#ifndef _CONFIGSCREEN_MAIN_H_
#define _SETTINGS_H_

#include <stdio.h>

class Settings 
{
public:
	Settings(void);

	bool getBool(char *key, bool defValue);
	void setBool(char *key, bool value);

	int  getInt(char *key, int defValue);
	void setInt(char *key, int value);
	
	char *getString(char *key, char *defValue);
	void  setString(char *key, char *value);
	
private:

	FILE *open(char *key, bool readNotWrite);
};

#endif