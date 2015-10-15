#include "StdAfx.h"

#ifdef _UNICODE
#include "../Common/StringConvert.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <string>

void split_path(const std::string &p_path, std::string &dir , std::string &base)
{
	size_t pos = p_path.find_last_of("/");
	if (pos == std::string::npos) {
		// no separator
		dir  = ".";
		if (p_path.empty()) base = ".";
		else              base = p_path;
	} else if ((pos+1) < p_path.size()) {
		// true separator
		base = p_path.substr(pos+1);
		while ((pos >= 1) && (p_path[pos-1] == '/')) pos--;
		if (pos == 0) dir = "/";
               	else          dir = p_path.substr(0,pos);
	} else {
		// separator at the end of the path
		pos = p_path.find_last_not_of("/");
		if (pos == std::string::npos) {
			base = "/";
                	dir = "/";
		} else {
			return split_path(p_path.substr(0,pos+1),dir,base);
		}
	}
}

#include "myPrivate.h"

// #define TRACEN(u) u;
#define TRACEN(u)  /* */

typedef struct {
#define IDENT_DIR_HANDLER 0x12345678
  int IDENT;
  DIR *dirp;
  std::string pattern;
  std::string directory;
}
t_st_dir;

static void remplirWIN32_FIND_DATA(WIN32_FIND_DATA *lpFindData,const char *rep,const char *nom) {
  struct stat stat_info;
  char filename[MAX_PATHNAME_LEN];
  int ret;
  snprintf(filename,MAX_PATHNAME_LEN,"%s/%s",rep,nom);
  filename[MAX_PATHNAME_LEN-1] = 0;
  ret = stat(filename,&stat_info);
  if (ret != 0) {
    printf("remplirWIN32_FIND_DATA(%s) : ERROR !?\n",filename);
    exit(EXIT_FAILURE);
  }
  memset(lpFindData,0,sizeof(*lpFindData));

  /* FIXME : FILE_ATTRIBUTE_HIDDEN */
  if (S_ISDIR(stat_info.st_mode))
    lpFindData->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
  else
    lpFindData->dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE;
  if (!(stat_info.st_mode & S_IWUSR))
    lpFindData->dwFileAttributes |= FILE_ATTRIBUTE_READONLY;

  RtlSecondsSince1970ToTime( stat_info.st_mtime, (LARGE_INTEGER *)&lpFindData->ftCreationTime );
  RtlSecondsSince1970ToTime( stat_info.st_mtime, (LARGE_INTEGER *)&lpFindData->ftLastWriteTime );
  RtlSecondsSince1970ToTime( stat_info.st_atime, (LARGE_INTEGER *)&lpFindData->ftLastAccessTime );

  lpFindData->nFileSizeHigh = 0;
  lpFindData->nFileSizeLow  = 0;
  if (!S_ISDIR(stat_info.st_mode)) {
    lpFindData->nFileSizeHigh = stat_info.st_size >> 32;
    lpFindData->nFileSizeLow  = stat_info.st_size & 0xffffffff;
  }
#ifdef _UNICODE
  UString ustr = MultiByteToUnicodeString(AString(nom));
  wcsncpy(lpFindData->cFileName,&ustr[0],MAX_PATH);
  lpFindData->cAlternateFileName[0] = 0;
#else
  strncpy(lpFindData->cFileName,nom,MAX_PATH);
  lpFindData->cAlternateFileName[0] = 0;
#endif
}

static int filtre_pattern(const char *string , const char *pattern , int flags_nocase) {
  if ((string == 0) || (*string==0)) {
    if (pattern == 0)
      return 1;
    while (*pattern=='*')
      ++pattern;
    return (!*pattern);
  }

  switch (*pattern) {
  case '*':
    if (!filtre_pattern(string+1,pattern,flags_nocase))
      return filtre_pattern(string,pattern+1,flags_nocase);
    return 1;
  case 0:
    if (*string==0)
      return 1;
    break;
  case '?':
    return filtre_pattern(string+1,pattern+1,flags_nocase);
  default:
    if (   ((flags_nocase) && (tolower(*pattern)==tolower(*string)))
           || (*pattern == *string)
       ) {
      return filtre_pattern(string+1,pattern+1,flags_nocase);
    }
    break;
  }
  return 0;
}

HANDLE WINAPI FindFirstFileA(LPCSTR lpFileName, WIN32_FIND_DATA *lpFindData ) {
  char cb[MAX_PATHNAME_LEN];
  t_st_dir *retour;

  if (!lpFileName) {
    SetLastError(ERROR_PATH_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
  }
 
  nameWindowToUnixA(lpFileName,cb);

  std::string base,dir;
  split_path(cb,dir,base);

  TRACEN((printf("FindFirstFileA : %s (dirname=%s,pattern=%s)\n",lpFileName,dir.c_str(),base.c_str())))

  retour = new t_st_dir;
  if (retour == 0) {
    SetLastError( ERROR_NO_MORE_FILES );
    return INVALID_HANDLE_VALUE;
  }

  retour->IDENT = IDENT_DIR_HANDLER;
  retour->dirp = opendir(dir.c_str());
  TRACEN((printf("FindFirstFileA : dirp=%p\n",retour->dirp)))
  retour->directory = dir;
  retour->pattern   = base;

  if (retour->dirp) {
    struct dirent *dp;
    while ((dp = readdir(retour->dirp)) != NULL) {
      if (filtre_pattern(dp->d_name,retour->pattern.c_str(),0) == 1) {
        remplirWIN32_FIND_DATA(lpFindData,retour->directory.c_str(),dp->d_name);
        TRACEN((printf("FindFirstFileA : ret_handle=%ld\n",(unsigned long)retour)))
        return (HANDLE)retour;
      }
    }
  }
  TRACEN((printf("FindFirstFileA : closedir(dirp=%p)\n",retour->dirp)))
  closedir(retour->dirp);
  delete retour;
  SetLastError( ERROR_NO_MORE_FILES );
  return INVALID_HANDLE_VALUE;
}

BOOL WINAPI FindNextFileA( HANDLE handle, WIN32_FIND_DATA  *lpFindData) {
  t_st_dir *retour = (t_st_dir *)handle;

  if ((handle == 0) || (handle == INVALID_HANDLE_VALUE) || (retour->IDENT != IDENT_DIR_HANDLER)) {
    SetLastError( ERROR_INVALID_HANDLE );
    return FALSE;
  }

  if (retour->dirp) {
    struct dirent *dp;
    while ((dp = readdir(retour->dirp)) != NULL) {
      if (filtre_pattern(dp->d_name,retour->pattern.c_str(),0) == 1) {
        remplirWIN32_FIND_DATA(lpFindData,retour->directory.c_str(),dp->d_name);
        return TRUE;
      }
    }
  }

  SetLastError( ERROR_NO_MORE_FILES );
  return FALSE;
}

BOOL WINAPI FindClose( HANDLE handle ) {
  TRACEN((printf("FindClose(%ld)\n",(unsigned long)handle)))
  if (handle != INVALID_HANDLE_VALUE) {
    t_st_dir *retour = (t_st_dir *)handle;
    if ((retour) && (retour->IDENT == IDENT_DIR_HANDLER)) {
      TRACEN((printf("FindClose : closedir(dirp=%p)\n",retour->dirp)))
      closedir(retour->dirp);
      delete retour;
      return TRUE;
    }
  }
  SetLastError( ERROR_INVALID_HANDLE );
  return FALSE;
}


