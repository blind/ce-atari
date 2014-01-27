#ifndef _MOUNTER_H_
#define _MOUNTER_H_

#include <string>

typedef struct {
	bool		deviceNotShared;
	
	std::string	devicePath;				// used when deviceNotShared is true
	
	struct {							// used when deviceNotShared is false
		std::string host;
		std::string hostDir;
		bool		nfsNotSamba;
	} shared;

	std::string mountDir;				// location where it should be mounted
	bool		mountNorUmount;			// action: true means mount, false means umount
} TMounterRequest;

extern "C" {
	void mountAdd(TMounterRequest &tmr);
	void *mountThreadCode(void *ptr);
}

class Mounter 
{
public:
	bool mountShared(char *host, char *hostDir, bool nfsNotSamba, char *mountDir);
	bool mountDevice(char *devicePath, char *mountDir);
	void umountIfMounted(char *mountDir);
	
private:
	bool mount(char *mountCmd, char *mountDir);

	bool isAlreadyMounted(char *source);
	bool isMountdirUsed(char *mountDir);
	bool tryUnmount(char *mountDir);
	
	void createSource(char *host, char *hostDir, bool nfsNotSamba, char *source);
	
	bool mountDumpContains(char *searchedString);
};

#endif

