/*
 * Incremental Snapshot
 *
 * Copyright (C) 2006, Joshua D. Henderson <www.digitalpeer.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/**
 * @file
 * @author Joshua D. Henderson
 *
 * isnapshot performs incremental backups by copying changed files and
 * symlinking unchanged files to a previous full backup. The result
 * is an always available incremental backup with minimal wasted space.
 *
 * It's meant to be run as a cron job or manually to create quick
 * snapshots of working directories or to do full system backups.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utime.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <time.h>
#include <libgen.h>
#include <getopt.h>
#include <fnmatch.h>
#include <stdbool.h>

#ifndef PATH_MAX
#define PATH_MAX 2048
#endif

static bool verbose = false;
static bool force_copy = false;
static bool count_bytes = false;
static off_t total_bytes = 0;
static off_t bytes_copied = 0;
static const char* date_format = "%m-%d-%y-%H-%M-%S";
static const char* exclude_pattern = NULL;

#define err(format, arg...)						\
   do {									\
      if (verbose)							\
	 fprintf(stderr,"error:%d " format "\n", __LINE__, ## arg);	\
      else								\
	 fprintf(stderr,"error: " format "\n", ## arg);			\
   } while (0);


#define info(format, arg...)						\
   do {									\
      if (verbose)							\
	 printf(format "\n", ## arg);					\
   } while (0);


/**
 * Recursive mkdir().
 */
static int rmkdir(char* path, int mode)
{
   int ret = 0;
   char* parent;
   struct stat stat_buf;

   char* dir = strdup(path);

   parent = dirname(dir);
   if (stat(parent, &stat_buf) < 0 && rmkdir(parent, mode) < 0)
   {
      err("could not create dir %s", parent);
      ret = -1;
      goto done;
   }

   if (stat(path, &stat_buf) >= 0)
   {
      if (S_ISDIR(stat_buf.st_mode))
	 goto done;
   }

   if (mkdir(path, mode) < 0)
   {
      err("could not create dir %s", dir);
      ret = -1;
   }
   else
   {
      info("mkdir %s",path);
   }

 done:
   free(parent);
   return ret;
}

static inline bool ignore_dir(const char* name)
{
   return (name && *name == '.' && (!name[1] || (name[1] == '.' && !name[2])));
}

/**
 * Joins file path parts. Returned memory must be free'd.
 */
static char* join_path(const char *path, const char *filename)
{
   char* result = NULL;

   if (path && filename)
   {
      result = (char*)malloc(strlen(path) + strlen(filename) + 2);

      if (result)
      {
	 strcpy(result,path);
	 if (*(path+(strlen(path)-1)) != '/')
	    strcat(result,"/");

	 while (*filename == '/')
	    filename++;

	 strcat(result,filename);
      }
   }

   return result;
}

/**
 * Find the previous incremental backup under the root dest path.
 */
static char* locate_previous(const char* root)
{
   char* result = NULL;
   DIR* dir = opendir(root);

   if (!dir)
   {
      err("could not open root directory %s", root);
      return NULL;
   }

   time_t latest = 0;

   /*
    * This is a bit of a hack, but just find the directory
    * with the last created date.
    */
   struct dirent* entry;
   while ((entry = readdir(dir)) != NULL)
   {
      if (ignore_dir(entry->d_name))
	 continue;

      struct tm t;

      char* res = strptime(entry->d_name,date_format,&t);

      if (!res || *res)
	 continue;

      time_t current = mktime(&t);

      if (current > latest)
      {
	 free(result);
	 result = join_path(root,entry->d_name);
	 latest = current;
      }
   }
   closedir(dir);

   return result;
}

/**
 * Set stat time, permissions, and ownership on file.
 */
bool copy_time(const char* file, struct stat* s)
{
   bool result = true;
   struct utimbuf times;

   times.actime = s->st_atime;
   times.modtime = s->st_mtime;

   if (utime(file, &times) < 0)
   {
      err("could not set time on %s", file);
      result = false;
   }

   if (chown(file, s->st_uid, s->st_gid) < 0)
   {
      err("could not set ownership on %s", file);
      s->st_mode &= ~(S_ISUID | S_ISGID);
      result = false;
   }

   if (chmod(file, s->st_mode) < 0)
   {
      err("could not set permissions on %s", file);
      result = false;
   }

   return result;
}

/**
 * Simple file copy with mode to set on new file.
 */
bool copy_file(const char* source, const char* dest, struct stat* s)
{
   bool result = true;

   info("copy %s ...",dest);

   int in = open(source, O_RDONLY);
   if (in == -1)
   {
      err("unable to open `%s'", source);
      result = false;
      goto done;
   }

   int out = open(dest, O_WRONLY|O_CREAT, s->st_mode);
   if (out == -1)
   {
      err("unable to open `%s'", dest);
      result = false;
      goto done;
   }

   char* buffer = (char*)malloc(s->st_blksize);

   if (!buffer)
   {
      result = false;
   }
   else
   {
      ssize_t bytes = 0;

      while((bytes = read(in, buffer, sizeof(buffer))) > 0)
      {
	 if (write(out, buffer, bytes) != bytes)
	 {
	    err("incomplete copy of file %s", source);
	    result = false;
	    break;
	 }
      }
   }

done:
   close(in);
   close(out);
   free(buffer);
   return result;
}

/**
 * Create a symlink. However, if the source is already a symlink,
 * use that symlink's source as the source, not the symlink itself.
 * We have to do this to prevent running into the nested symlink
 * limitation.
 */
static inline bool symlink_file(const char* source, const char* dest)
{
   char buffer[PATH_MAX+1];
   struct stat s;

   memset(buffer,0,sizeof(buffer));

   if (lstat(source, &s) < 0)
   {
      err("could not stat %s", source);
      return false;
   }

   if (S_ISLNK(s.st_mode))
   {
      if (readlink(source,buffer,PATH_MAX) == -1)
      {
	 err("cannot read symlink `%s'", source);
	 return false;
      }
      else
      {
	 source = buffer;
      }
   }

   info("mirror %s ...",source);

   return symlink(source, dest) == 0;
}

static const char* current_time(const char* format)
{
   static char date[1024];
   time_t now;
   struct tm tnow;
   time(&now);
   tnow = *localtime(&now);
   strftime(date,32,format,&tnow);
   return date;
}

/**
 * Process a file (or directory, or symlink, etc).
 *
 * @param source Complete path to the source file.
 * @param root Path to the backup destination directory.
 * @param prev Optional path to a previous backup destination directory.
 */
bool process_file(const char* source, const char* root, const char* prev_root)
{
   bool result = true;
   struct stat source_stat;

   if (lstat(source, &source_stat) < 0)
   {
      err("could not stat file %s", source);
      result = false;
   }
   else
   {
      if (exclude_pattern && !fnmatch(exclude_pattern,source,0))
	 return true;

      /* destination file */
      char* dest = join_path(root,source);

      /* optional previous file path */
      char* prev_dest = prev_root ? join_path(prev_root,source) : NULL;

      if (S_ISDIR(source_stat.st_mode))
      {
	 mode_t saved_umask = umask(0);
	 mode_t mode = source_stat.st_mode |= S_IRWXU;

	 if (rmkdir(dest, mode) < 0)
	 {
	    err("cannot create directory %s", dest);
	    umask(saved_umask);
	    result = false;
	 }
	 else
	 {
	    umask(saved_umask);

	    DIR* dir = opendir(source);

	    if (!dir)
	    {
	       err("could not open directory %s", source);
	       result = false;
	    }
	    else
	    {
	       struct dirent* entry;
	       while ((entry = readdir(dir)) && result)
	       {
		  if (ignore_dir(entry->d_name))
		     continue;

		  char* new_source = join_path(source,entry->d_name);

		  result = process_file(new_source, root, prev_root);

		  free(new_source);
	       }
	       closedir(dir);
	    }

	    if (result && chmod(dest, source_stat.st_mode & ~saved_umask) < 0)
	    {
	       err("unable to change permissions of `%s'", dest);
	       result = false;
	    }
	    else if (result)
	    {
	       result = copy_time(dest,&source_stat);
	    }
	 }
      }
      else if (S_ISREG(source_stat.st_mode))
      {
	 if (count_bytes)
	 {
	    total_bytes += source_stat.st_size;
	 }

	 /*
	  * If the current file has a different modification time than the previous file,
	  * do a fresh copy, otherwise symlink to previous backup.
	  */
	 struct stat prev_stat;
	 if (!prev_dest || force_copy || stat(prev_dest, &prev_stat) < 0 ||
	     (source_stat.st_mtime != prev_stat.st_mtime))
	 {
	    result = copy_file(source,dest,&source_stat) && copy_time(dest,&source_stat);

	    if (count_bytes)
	    {
	       bytes_copied += source_stat.st_size;
	    }
	 }
	 else
	 {
	    result = symlink_file(prev_dest,dest);
	 }
      }
      else if (S_ISBLK(source_stat.st_mode) || S_ISCHR(source_stat.st_mode) ||
	       S_ISSOCK(source_stat.st_mode) || S_ISFIFO(source_stat.st_mode) ||
	       S_ISLNK(source_stat.st_mode))
      {

	 if (S_ISFIFO(source_stat.st_mode))
	 {
	    if (mkfifo(dest, source_stat.st_mode) < 0)
	    {
	       err("cannot create fifo `%s'", dest);
	       result = false;
	    }
	    else
	    {
	       info("fifo %s",source);
	    }
	 }
	 else if (S_ISLNK(source_stat.st_mode))
	 {
	    char buffer[PATH_MAX+1];
	    memset(buffer,0,sizeof(buffer));

	    if (readlink(source,buffer,PATH_MAX) == -1)
	    {
	       err("cannot read symlink `%s'", source);
	       result = false;
	    }
	    else if (symlink(buffer, dest) < 0)
	    {
	       err("cannot create symlink `%s'", dest);
	       result = false;
	    }
	    else if (lchown(dest, source_stat.st_uid, source_stat.st_gid) < 0)
	    {
	       err("unable to preserve ownership of `%s'", dest);
	       result = false;
	    }
	    else
	    {
	       info("symlink %s",source);
	    }
	 }
	 else
	 {
	    if (mknod(dest, source_stat.st_mode, source_stat.st_rdev) < 0)
	    {
	       err("unable to create node `%s'", dest);
	       result = false;
	    }
	    else
	    {
	       info("node %s",source);
	    }
	 }
      }
      else
      {
	 err("unrecognized file type");
	 result = false;
      }

      free(dest);
      free(prev_dest);

   }

   return result;
}

static void usage(const char* base)
{
   fprintf(stderr,
	   "Incremental Snapshot Version 1.0\n"				\
	   "Usage: %s [OPTION] SOURCE... DESTINATION\n"			\
	   "   -h,--help                  Show this menu.\n"		\
	   "   -v,--verbose               Show verbose information.\n"	\
	   "   -f,--full                  Perform full backup. Default is incremental.\n" \
	   "   -c,--count-bytes           Count the number of bytes copied compared to total backup.\n" \
	   "   -d,--date-format=FORMAT    Set backup folder date format (default %s).\n" \
	   "   -e,--exclude=PATTERN       Define exclude pattern to exlude files from snapshot.\n" \
	   "\n",base,date_format);
}

const char short_options[] = "d:e:fvhc";

struct option long_options[] =
{
   { "verbose",      0, 0, 'v' },
   { "full",         0, 0, 'f' },
   { "date-format",  1, 0, 'd' },
   { "exclude",      1, 0, 'e' },
   { "count-bytes",  0, 0, 'c' },
   { "help",         0, 0, 'h' },
   { 0,              0, 0, 0   }
};

int main(int argc, char** argv)
{
   int result = 0;
   int n;
   int x;
   struct stat stat_buf;

   while((n=getopt_long(argc,argv,short_options,long_options, NULL)) != -1)
   {
      switch(n)
      {
      case 0:
	 break;
      case 'v':
	 verbose = true;
	 break;
      case 'd':
	 date_format = optarg;
	 break;
      case 'e':
	 exclude_pattern = optarg;
	 break;
      case 'c':
	 count_bytes = true;
	 break;
      case 'f':
	 force_copy = true;
	 break;
      case 'h':
	 usage(argv[0]);
	 return 0;
      default:
	 usage(argv[0]);
	 return 1;
      }
   }

   if (argc - optind < 2)
   {
      err("not enough arguments");
      usage(argv[0]);
      return 1;
   }

   const char* root = argv[argc-1];
   char* previous = locate_previous(root);
   char* dest = join_path(root,current_time(date_format));

   info("backing up to %s",dest);

   /**
    * @todo Make sure destination does not contain any of the sources.
    */

   if (stat(dest, &stat_buf) == 0)
   {
      err("backup already exists for %s",dest);
      result = 1;
      goto done;
   }

   /*
    * Create new backup directory.
    */
   if (rmkdir(dest, 0755) < 0)
   {
      err("could not create directory %s",dest);
      result = 1;
      goto done;
   }

   if (previous)
   {
      info("using previous backup at %s",previous);
   }

   for (x = optind; x < argc-1;x++)
   {
      if (!process_file(argv[x],dest,previous))
      {
	 result = 1;
	 break;
      }
   }

   if (count_bytes && !result)
   {
      printf("Copied %d of %d bytes total in backup.\n",bytes_copied,total_bytes);
   }

 done:

   free(previous);
   free(dest);

   return result;
}
