/*
 * Copyright (C) 2012-2021 AirDC++ Project
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

#include "stdinc.h"

#include <airdcpp/DCPlusPlus.h>
#include <airdcpp/util/AppUtil.h>
#include <airdcpp/core/classes/Exception.h>
#include <airdcpp/util/Util.h>
#include <airdcpp/util/SystemUtil.h>
#include <airdcpp/core/version.h>
#include <airdcpp/core/io/File.h>

#include <web-server/WebServerManager.h>

#include "Client.h"
#include "ConfigPrompt.h"
#include "stacktrace.h"

#include <signal.h>
#include <limits.h>
#include <locale.h>
#include <fstream>


using namespace dcpp;

static std::unique_ptr<File> pidFile;
static std::string pidFileName;
static bool asdaemon = false;
static bool crashed = false;
static std::unique_ptr<airdcppd::Client> client;

static void installHandler();

static void uninit() {
	//if(!asdaemon)
	//	printf("Shut down\n");

	pidFile.reset(nullptr);
	if(!pidFileName.empty())
		unlink(pidFileName.c_str());
}

static void handleCrash(int sig) {
	if(crashed)
		abort();

	crashed = true;

	uninit();

        std::cerr << std::endl << std::endl;
        std::cerr << "Signal: " << std::to_string(sig) << std::endl;
        std::cerr << "Process ID: " << getpid() << std::endl;
        std::cerr << "Time: " << Util::formatCurrentTime() << std::endl;
        std::cerr << "OS version: " << SystemUtil::getOsVersion() << std::endl;
        std::cerr << "Client version: " << shortVersionString << std::endl << std::endl;
#if USE_STACKTRACE
	std::cerr << "Collecting crash information, please wait..." << std::endl;
	cow::StackTrace trace(AppUtil::getAppPath());
	trace.generate_frames();
	std::copy(trace.begin(), trace.end(),
	std::ostream_iterator<cow::StackFrame>(std::cerr, "\n"));

	auto stackPath = AppUtil::getPath(AppUtil::PATH_USER_CONFIG) + "exceptioninfo.txt";
	std::ofstream f;
	f.open(stackPath.c_str());

	f << "Time: " + Util::formatCurrentTime() << std::endl;
	f << "OS version: " + SystemUtil::getOsVersion() << std::endl;
	f << "Client version: " + shortVersionString << std::endl << std::endl;

	std::copy(trace.begin(), trace.end(),
	std::ostream_iterator<cow::StackFrame>(f, "\n"));
	f.close();
	std::cout << "\nException info to be posted on the bug tracker has also been saved in " + stackPath << std::endl;
#else
	std::cout << std::endl;
	std::cout << "Stacktrace is not available" << std::endl;
	std::cout << "Please see https://github.com/airdcpp-web/airdcpp-webclient/blob/master/.github/CONTRIBUTING.md#application-crashes" << std::endl;
	std::cout << "for information about getting the crash log to post on the bug tracker" << std::endl;
#endif
	if (!asdaemon) {
		std::cout << std::endl;
#if USE_STACKTRACE
		std::cout << "Press enter to exit" << std::endl;
#else
		std::cout << "Attach debugger in a separate terminal window to get the necessary information for the bug report and press enter to exit when you are finished" << std::endl;
#endif


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
	// sigdelset(&mask, SIGBUS);
	sigdelset(&mask, SIGINT);
	sigdelset(&mask, SIGTRAP);
	//pthread_sigmask(SIG_SETMASK, &mask, NULL);

	installHandler();
}

static void installHandler() {
	//struct sigaction sa = { 0 };

	//sa.sa_handler = breakHandler;

	//sigaction(SIGINT, &sa, NULL);

	signal(SIGINT, &breakHandler);
	signal(SIGTERM, &breakHandler);

	signal(SIGBUS, &handleCrash);
	signal(SIGFPE, &handleCrash);
	signal(SIGSEGV, &handleCrash);
	signal(SIGILL, &handleCrash);

	signal(SIGPIPE, SIG_IGN);

	// Note: separate from SIGTERM
	std::set_terminate([] {
		handleCrash(0);
	});
}

static void setPidFilePath(const string& aConfigPath, const dcpp::StartupParams& aStartupParams) {
    auto pidParam = aStartupParams.getValue("-p");
    if (pidParam) {
        pidFileName = *pidParam;
    } else {
        pidFileName = File::makeAbsolutePath(aConfigPath) + "airdcppd.pid";
    }
}

static void savePid(int aPid) noexcept {
	try {
		pidFile.reset(new File(pidFileName, File::WRITE, File::CREATE | File::OPEN | File::TRUNCATE));
		pidFile->write(Util::toString(aPid));
	} catch(const FileException& e) {
		fprintf(stderr, "Failed to create PID file %s: %s\n", pidFileName.c_str(), e.what());
		exit(1);
	}
}

static void reportError(const char* aMessage) noexcept {
	fprintf(stderr, (string(aMessage) + ": %s\n").c_str(), strerror(errno));
}

#include <fcntl.h>

static void daemonize(const dcpp::StartupParams& aStartupParams) noexcept {
	auto doFork = [&](const char* aErrorMessage) {
		auto ret = fork();

		switch(ret) {
		case -1:
			reportError(aErrorMessage);
			exit(5);
		case 0: break;
		default:
			savePid(ret);
			//printf("%d\n", ret); fflush(stdout);
			_exit(0);
		}
	};

	doFork("First fork failed");

	if(setsid() < 0) {
		reportError("setsid failed");
		exit(6);
	}

	doFork("Second fork failed");

	if (chdir("/") < 0) {
		reportError("chdir failed");
		exit(8);
	}

	close(0);
	close(1);
	close(2);

	open("/dev/null", O_RDWR);

	if (dup(0) < 0) {
		reportError("dup failed for stdout");
		exit(9);
	}

	if (dup(0) < 0) {
		reportError("dup failed for stderr");
		exit(10);
	}
}

#include <sys/wait.h>

static void runDaemon(const dcpp::StartupParams& aStartupParams) {
	daemonize(aStartupParams);

	try {
		client = unique_ptr<airdcppd::Client>(new airdcppd::Client(asdaemon));

		init();

		client->run(aStartupParams);

		client.reset();
	} catch(const std::exception& e) {
		fprintf(stderr, "Failed to start: %s\n", e.what());
	}

	uninit();
}

static void runConsole(const dcpp::StartupParams& aStartupParams) {
	printf("Starting.\n"); fflush(stdout);

	savePid(static_cast<int>(getpid()));

	try {
		client = unique_ptr<airdcppd::Client>(new airdcppd::Client(asdaemon));
		printf("."); fflush(stdout);

		init();

		client->run(aStartupParams);

		client.reset();
	} catch(const std::exception& e) {
		fprintf(stderr, "\nFATAL: Can't start AirDC++ Web Client: %s\n", e.what());
	}
	uninit();
}

#define HELP_WIDTH 25
static void printUsage() {
	printf("Usage: airdcppd [options]\n");

	auto printHelp = [](const std::string& aCommand, const std::string& aHelp) {
		std::cout << std::left << std::setw(HELP_WIDTH) << std::setfill(' ') << aCommand;
		std::cout << std::left << std::setw(HELP_WIDTH) << std::setfill(' ') << aHelp << std::endl;
	};

	cout << std::endl;
	printHelp("-h", 								"Print help");
	printHelp("-v", 								"Print version");
	printHelp("-d", 								"Run as daemon");
	printHelp("-p=PATH",								"Custom pid file path (default: <CFG_DIR>/.airdcppd.pid)");
	printHelp("-c=PATH", 						"Use the specified config directory for client settings");

	cout << std::endl;
	printHelp("--no-autoconnect", 	"Don't connect to any favorite hub on startup");
	printHelp("--cdm-hub", 					"Print all protocol communication with hubs in the console (debug)");
	printHelp("--cdm-client", 			"Print all protocol communication with other clients in the console (debug)");
	printHelp("--cdm-web", 					"Print web API commands and file requests in the console (debug)");


	cout << std::endl;
	cout << std::endl;
	cout << "Web server" << std::endl;
	cout << std::endl;
	printHelp("--configure", 					"Run initial config wizard or change server ports");
	printHelp("--add-user", 					"Add a new web user with administrative permissions (or change password for existing users)");
	printHelp("--remove-user", 				"Remove web user");
	printHelp("--web-resources=PATH", "Use the specified resource directory for web server files");
	cout << std::endl;
}

static void setApp(char* argv[]) {
	char buf[PATH_MAX + 1] = { 0 };
	char* path = buf;
	if (readlink("/proc/self/exe", buf, sizeof (buf)) == -1) {
		path = getenv("_");
	}

	AppUtil::setApp(path == NULL ? argv[0] : path);
}

int main(int argc, char* argv[]) {
	setApp(argv);

    dcpp::StartupParams startupParams;
	while (argc > 0) {
        startupParams.addParam(Text::toUtf8(*argv));
		argc--;
		argv++;
	}

	if (startupParams.hasParam("-h") || startupParams.hasParam("--help")) {
		printUsage();
		return 0;
	}

	if (startupParams.hasParam("-v") || startupParams.hasParam("--version")) {
		printf("%s\n", shortVersionString.c_str());
		return 0;
	}

    {
        auto customConfigDir = startupParams.getValue("-c");
        initializeUtil(customConfigDir ? *customConfigDir : "");
    }
	auto configF = airdcppd::ConfigPrompt::checkArgs(startupParams);
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

	if (startupParams.hasParam("-d")) {
		asdaemon = true;
	}


	setlocale(LC_ALL, "");

	string configPath = AppUtil::getPath(AppUtil::PATH_USER_CONFIG);
    setPidFilePath(configPath, startupParams);
	if (asdaemon) {
		runDaemon(startupParams);
	} else {
		runConsole(startupParams);
	}
}
