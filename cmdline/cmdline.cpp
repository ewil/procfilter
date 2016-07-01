


#include "procfilter/procfilter.h"

#include <winternl.h>

typedef struct match_data SCAN_DATA;
struct match_data {
	bool   bCaptureCommandLine; // Should the command line associated with this process be captured?
	WCHAR *lpszCommandLine;     // A copy of the command line if captured
};


//
// Store the command line associated with the current process into the SCAN_DATA structure.
//
WCHAR*
CaptureCommandLine(PROCFILTER_EVENT *e)
{
	// read the processes PEB and it's Parameters structure
	WCHAR *lpszResult = NULL;
	PEB Peb;
	RTL_USER_PROCESS_PARAMETERS Parameters;
	if (e->ReadProcessPeb(&Peb) && e->ReadProcessMemory(Peb.ProcessParameters, &Parameters, sizeof(Parameters))) {
		// check to make sure the command line is present
		DWORD len = Parameters.CommandLine.Length;
		if (len > 0) {
			// allocate memory for the command line and then copy it out from the remote process
			lpszResult = (WCHAR*)e->AllocateMemory(len + 1, sizeof(WCHAR));
			if (lpszResult && e->ReadProcessMemory(Parameters.CommandLine.Buffer, lpszResult, len)) {
				lpszResult[len] = '\0';
			} else if (lpszResult) {
				e->FreeMemory(lpszResult);
				lpszResult = NULL;
			}
		}
	}

	return lpszResult;
}


//
// ProcFilter event handler
//
DWORD
ProcFilterEvent(PROCFILTER_EVENT *e)
{
	DWORD dwResultFlags = PROCFILTER_RESULT_NONE;
	SCAN_DATA *sd = (SCAN_DATA*)e->lpvScanData;
	
	if (e->dwEventId == PROCFILTER_EVENT_INIT) {
		// register the plugin with the core
		e->RegisterPlugin(PROCFILTER_VERSION, L"CommandLine", 0, sizeof(SCAN_DATA), false,
			PROCFILTER_EVENT_YARA_SCAN_INIT, PROCFILTER_EVENT_YARA_SCAN_COMPLETE, PROCFILTER_EVENT_YARA_SCAN_CLEANUP,
			PROCFILTER_EVENT_YARA_RULE_MATCH, PROCFILTER_EVENT_YARA_RULE_MATCH_META_TAG, PROCFILTER_EVENT_NONE);

	} else if (e->dwEventId == PROCFILTER_EVENT_YARA_SCAN_INIT) {
		// the match data buffer is zero-initialized by the core, but extra init can be done here if needed
	} else if (e->dwEventId == PROCFILTER_EVENT_YARA_RULE_MATCH) {
		// this event happens every time a rule is matched during a scan
	} else if (e->dwEventId == PROCFILTER_EVENT_YARA_RULE_MATCH_META_TAG) {
		// this event happens for each meta value name in every rule that matches
		// since we only care if this tag is set anywhere, its fine if this meta tag
		// is found in several rules
		if (_stricmp(e->lpszMetaTagName, "CaptureCommandLine") == 0 && e->dNumericValue) {
			sd->bCaptureCommandLine = true;
		}
	} else if (e->dwEventId == PROCFILTER_EVENT_YARA_SCAN_COMPLETE) {
		// here we can look at the match data as filled in during prior events and capture the command line accordingly
		// while the process is still suspended
		if (sd->bCaptureCommandLine) {
			sd->lpszCommandLine = CaptureCommandLine(e);
		}
	} else if (e->dwEventId == PROCFILTER_EVENT_YARA_SCAN_CLEANUP) {
		// here we examine the contents as filled in durring scanning, and handle our final results accordingly
		if (sd->bCaptureCommandLine && sd->lpszCommandLine) {
			e->LogFmt("Command line for %d %ls: %ls", e->dwProcessId, e->lpszFileName, sd->lpszCommandLine);

			// release previously allocated memory
			e->FreeMemory(sd->lpszCommandLine);
		}
	}

	return dwResultFlags;
}

