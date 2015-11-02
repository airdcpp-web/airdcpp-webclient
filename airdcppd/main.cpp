/*
 * Copyright (C) 2006-2015 Jacek Sieka, arnetheduck on gmail point com
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <airdcpp/stdinc.h>
#include <airdcpp/DCPlusPlus.h>
#include <airdcpp/Util.h>
#include <airdcpp/version.h>
#include <airdcpp/File.h>

#include <web-server/WebServerManager.h>

#include "Client.h"
#include "ConfigPrompt.h"
#include "stacktrace.h"

#include <signal.h>
#include <limits.h>
#include <locale.h>
#include <fstream>

using namespace std;

static FILE* pidFile;
static string pidFileName;
static bool asdaemon = false;
static bool crashed = false;
static unique_ptr<airdcppd::Client> client;

static void installHandler();

static void uninit() {
	//if(!asdaemon)
	//	printf("Shut down\n");

	if(pidFile != NULL)
		fclose(pidFile);
	pidFile = NULL;

	if(!pidFileName.empty())
		unlink(pidFileName.c_str());
}

static void handleCrash(int sig) {
    if(crashed)
        abort();

    crashed = true;
	
	uninit();

    std::cerr << std::to_string(sig) << std::endl;
    std::cerr << "pid: " << getpid() << std::endl;
#if USE_STACKTRACE
    std::cerr << "Collecting crash information, please wait..." << std::endl;
    cow::StackTrace trace(Util::getAppPath());
    trace.generate_frames();
    std::copy(trace.begin(), trace.end(),
        std::ostream_iterator<cow::StackFrame>(std::cerr, "\n"));

	auto stackPath = Util::getPath(Util::PATH_USER_CONFIG) + "exceptioninfo.txt";
	std::ofstream f;
	f.open(stackPath.c_str());

	f << "Time: " + Util::getTimeString() << std::endl;
	f << "OS version: " + Util::getOsVersion() << std::endl;
	f << "Client version: " + shortVersionString << std::endl << std::endl;

	std::copy(trace.begin(), trace.end(),
		std::ostream_iterator<cow::StackFrame>(f, "\n"));
	f.close();
	std::cout << "\nException info to be posted on the bug tracker has also been saved in " + stackPath << std::endl;
#else
    std::cerr << "Stacktrace is not enabled\n";
#endif
	//std::cout << "You can ignore this message if you exited with Ctrl+C and the client was functioning correctly (and please use /quit in future)" << std::endl;
	if (!asdaemon) {
		std::cout << "Press enter to continue" << std::endl;
		cin.ignore();
	}
	exit(sig);
}

void breakHandler(int) {
	if (client) {
		client->stop();
	}

	installHandler();
}

static void init() {
	// Ignore SIGPIPE...
	struct sigaction sa = { 0 };

	sa.sa_handler = SIG_IGN;

	sigaction(SIGPIPE, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	sigset_t mask;

	sigfillset(&mask); /* Mask all allowed signals, the other threads should inherit
					   this... */
	
	sigdelset(&mask, SIGCONT);
	sigdelset(&mask, SIGFPE);
	sigdelset(&mask, SIGBUS);
	sigdelset(&mask, SIGINT);
	sigdelset(&mask, SIGTRAP);
	//pthread_sigmask(SIG_SETMASK, &mask, NULL);

	installHandler();

	if(pidFile != NULL) {
		fprintf(pidFile, "%d", (int)getpid());
		fflush(pidFile);
	}
}

static void installHandler() {
	//struct sigaction sa = { 0 };

	//sa.sa_handler = breakHandler;

	//sigaction(SIGINT, &sa, NULL);
	
	signal(SIGINT, &breakHandler);
	
	signal(SIGFPE, &handleCrash);
	signal(SIGSEGV, &handleCrash);
	signal(SIGILL, &handleCrash);
	
	std::set_terminate([] {
		handleCrash(0);
	});
}

#include <fcntl.h>

static void daemonize() {
	switch(fork()) {
	case -1:
		fprintf(stderr, "First fork failed: %s\n", strerror(errno));
		exit(5);
	case 0: break;
	default: _exit(0);
	}

	if(setsid() < 0) {
		fprintf(stderr, "setsid failed: %s\n", strerror(errno));
		exit(6);
	}

	switch(fork()) {
		case -1:
			fprintf(stderr, "Second fork failed: %s\n", strerror(errno));
			exit(7);
		case 0: break;
		default: exit(0);
	}

	chdir("/");
	close(0);
	close(1);
	close(2);
	open("/dev/null", O_RDWR);
	dup(0); dup(0);
}

#include <sys/wait.h>

static void runDaemon(const string& configPath) {
	daemonize();

	try {
		client = unique_ptr<airdcppd::Client>(new airdcppd::Client(asdaemon));
		
		init();
		
		client->run();
		
		client.reset();
	} catch(const std::exception& e) {
		fprintf(stderr, "Failed to start: %s\n", e.what());
	}

	uninit();
}

static void runConsole(const string& configPath) {
	printf("Starting.\n"); fflush(stdout);

	try {
		client = unique_ptr<airdcppd::Client>(new airdcppd::Client(asdaemon));
		printf("."); fflush(stdout);
		
		init();
		
		client->run();
		
		client.reset();
	} catch(const std::exception& e) {
		fprintf(stderr, "\nFATAL: Can't start AirDC++ Web Client: %s\n", e.what());
	}
	uninit();
}

static void printUsage() {
	printf("Usage: airdcppd [[-c <configdir>] [-d]] | [-v] | [-h]\n");
}

static void setApp(char* argv[]) {
	char buf[PATH_MAX + 1] = { 0 };
	char* path = buf;
	if (readlink("/proc/self/exe", buf, sizeof (buf)) == -1) {
		path = getenv("_");
	}

	Util::setApp(path == NULL ? argv[0] : path);
}

int main(int argc, char* argv[]) {
	setApp(argv);
	
	while (argc > 0) {
		Util::addStartupParam(Text::fromT(*argv));
		argc--;
		argv++;
	}

	if (Util::hasStartupParam("-h")) {
		printUsage();
		return 0;
	}

	if (Util::hasStartupParam("-v")) {
		printf("%s\n", shortVersionString.c_str());
		return 0;
	}
	
	dcpp::Util::initialize();
	auto configF = airdcppd::ConfigPrompt::checkArgs();
	if (configF) {
		init();
		signal(SIGINT, [](int) {
			airdcppd::ConfigPrompt::setPasswordMode(false);
			cout << std::endl;
			uninit(); 
			exit(0); 
		});
		
		configF();
		
		uninit();
		return 0;
	}
	
	if (Util::hasStartupParam("-d")) {
		asdaemon = true;
	}
	
	if (Util::hasStartupParam("-p")) {
		auto p = Util::getStartupParam("-p");
		if (p) {
			pidFileName = *p;
		} else {
			fprintf(stderr, "-p <pid-file>\n");
			return 1;
		}
	}

	
	setlocale(LC_ALL, "");

	string configPath = Util::getPath(Util::PATH_USER_CONFIG);

	if(!pidFileName.empty()) {
		pidFileName = File::makeAbsolutePath(configPath, pidFileName);
		pidFile = fopen(pidFileName.c_str(), "w");
		if(pidFile == NULL) {
			fprintf(stderr, "Can't open %s for writing\n", pidFileName.c_str());
			return 1;
		}
	}

	if(asdaemon) {
		runDaemon(configPath);
	} else {
		runConsole(configPath);
	}
}
