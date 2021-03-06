#ifndef FILENAMESHORTENER_H
#define FILENAMESHORTENER_H

#include <iostream>
#include <map>

// Average size for one record seems to be around 130 bytes.

#define MAX_FILENAME_LEN    1024
#define MAX_FILEEXT_LEN     256

/*
problems:
- Include, include, inCluDe -> INCLUDE ---- mapReverseFilename musi byt multi-map
*/


class FilenameShortener
{
public:
    FilenameShortener();

    void clear(void);                                                       // clear maps - e.g. on ST restart

    bool longToShortFileName(const char *longFileName, char *shortFileName);      // translates 'long file name' to 'long_f~1'
    const bool shortToLongFileName(const char *shortFileName, char *longFileName);      // translates 'long_f~1' to 'long file name'

    static void mergeFilenameAndExtension(const char *shortFn, const char *shortExt, bool extendWithSpaces, char *merged);

    static void removeSpaceExtension(const char *extendedFn, char *extRemovedFn); // 'FILE    .C  ' -> 'FILE.C'
    static void extendWithSpaces(const char *normalFname, char *extendedFn);      // 'FILE.C'       -> 'FILE    .C  '
    static void splitFilenameFromExtension(const char *filenameWithExt, char *fileName, char *ext);

private:
    std::map<std::string, std::string>  mapFilenameWithExt;                 // for file name conversion from long to short
    std::map<std::string, std::string>  mapReverseFilename;                 // for file name conversion from short to long

    std::map<std::string, std::string> mapFilenameNoExt;                    // used by shortenName() to create unique file name with ~
    bool allowExtUse;          // Allow use of Extension for shortening (if file without extension)

    const bool shortenName(const char *nLong, char *nShort);
    const bool shortenExtension(const char *shortFileName, const char *nLongExt, char *nShortExt);
    const bool shortenNameUsingExt(const char *fileName, char *shortName, char *shortExt);

    static int  strrCharPos(const char *str, int maxLen, char ch);
    static void replaceNonLetters(char *str);
    static void extendToLenghtWithSpaces(char *str, int len);
    static void removeTrailingSpaces(char *str);
};

#endif // FILENAMESHORTENER_H
