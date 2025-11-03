#### INSTALLATION
Before installing the wrapper, you have to make sure that `CMake` is installed on your system. It can be downloaded from [here](https://cmake.org/download/).

You can install by using CMake with `FetchContent`. Add the following to your `CMakeLists.txt`:
```cmake
include(FetchContent)
FetchContent_Declare(
    ZeroBounce
    GIT_REPOSITORY https://github.com/zerobounce/zerobounce-cpp-api-wrapper.git
    GIT_TAG <DESIRED_TAG>
)
FetchContent_MakeAvailable(ZeroBounce)
```

This will produce the target `ZeroBounce` which you can link against the typical way:

```cmake
target_link_libraries(your_target_name PRIVATE ZeroBounce)
```

If using Visual Studio Code, you can build the library using `CMake` and `Cmake Tools` extensions. After the extensions have been installed, you simply open the commands with `CTRL + SHIFT + P` and use `CMake: Configure` followed by `CMake: Build`.

Alternatively, you can also use the CLI commands in the root directory of your project.

###### LINUX

```bash
mkdir build
cmake -S . -B build -G "Unix Makefiles"
cmake --build build
```

###### WINDOWS

You have to make sure that `MinGW` is installed on your system. It can be installed following the steps [here](https://code.visualstudio.com/docs/cpp/config-mingw).

```bash
mkdir build
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

Be aware that the library may require some .dll files (found in the `build` directory) in order to work properly in your project.

#### USAGE
Include the library in your file:
```cpp
###include <ZeroBounce/ZeroBounce.h>
```

Initialize the wrapper with your api key and preferred api:
```cpp
ZeroBounce::getInstance()->initialize("<YOUR_API_KEY>", ZBApiURL::Default);
```

#### Examples
Then you can use any of the wrapper methods, for example:
* ####### Check how many credits you have left on your account
```cpp
ZeroBounce::getInstance()->getCredits(
    [](ZBCreditsResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);
```

* ####### Check your API usage for a given period of time
```cpp
std::tm startDate = {0};
startDate.tm_year = 118;
startDate.tm_mon = 0;
startDate.tm_mday = 1;

std::tm endDate = {0};
endDate.tm_year = 123;
endDate.tm_mon = 11;
endDate.tm_mday = 12;

ZeroBounce::getInstance()->getApiUsage(
    startDate,
    endDate,
    [](ZBGetApiUsageResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);
```

* ####### Validate an email address
```cpp
std::string email = "valid@example.com";   // The email address you want to validate
std::string ipAddress = "127.0.0.1";     // The IP Address the email signed up from (Optional)

ZeroBounce::getInstance()->validate(
    email,
    ipAddress,
    [](ZBValidateResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);
```

* ####### Validate email batch
```cpp
std::vector<ZBEmailToValidate> emails = {
    {"valid@example.com", "1.1.1.1"},
    {"invalid@example.com"}
};

ZeroBounce::getInstance()->validateBatch(
    emails,
    [](ZBValidateBatchResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);
```

* ####### Use the Activity API endpoint to gather insights into your subscribers'overall email engagement
```cpp
std::string email = "valid@example.com";    // Subscriber email address

ZeroBounce::getInstance()->getActivityData(
    email,
    [](ZBActivityDataResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);
```

* ####### The sendfile API allows user to send a file for bulk email validation
```cpp
std::string filePath = "<FILE_PATH>"; 	// The path of the csv or txt file
int emailAddressColumn = 3; 			// The index of "email" column in the file. Index starts at 1
SendFileOptions options;			// Additional options

options.returnUrl = "https://domain.com/called/after/processing/request";
options.firstNameColumn = 4;		// The index of "first name" column in the file
options.lastNameColumn = 5;		// The index of "last name" column in the file
options.genderColumn = 6;		// The index of "gender" column in the file
options.ipAddressColumn = 7;		// The index of "IP address" column in the file
options.hasHeaderRow = true;		// If this is `true` the first row is considered as table headers
options.removeDuplicate = true;	// If this is `true` the duplicate emails will be removed

ZeroBounce::getInstance()->sendFile(
    filePath,
    emailAddressColumn,
    options,
    [](ZBSendFileResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);
```

* ####### The getfile API allows users to get the validation results file for the file been submitted using sendfile API
```cpp
std::string fileId = "<FILE_ID>";				// The returned file ID when calling sendfile API
std::string localDownloadPath = "<FILE_DOWNLOAD_PATH>"; 	// The location where the downloaded file will be saved

ZeroBounce::getInstance()->getFile(
    fileId,
    localDownloadPath,
    [](ZBGetFileResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);
```

* ####### Check the status of a file uploaded via "sendFile" method
```cpp
std::string fileId = "<FILE_ID>";		// The returned file ID when calling sendfile API

ZeroBounce::getInstance()->fileStatus(
    fileId,
    [](ZBFileStatusResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);
```

* ####### Deletes the file that was submitted using sendfile API. File can be deleted only when its status is _`Complete`_
```cpp
std::string fileId = "<FILE_ID>";		// The returned file ID when calling sendfile API

ZeroBounce::getInstance()->deleteFile(
    fileId,
    [](ZBDeleteFileResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);
```

* ####### The scoringSendfile API allows user to send a file for bulk email validation
```cpp
std::string filePath = "<FILE_PATH>";		// The path of the csv or txt file
int emailAddressColumn = 3; 			// The index of "email" column in the file. Index starts at 1
SendFileOptions options;			// Additional options

options.returnUrl = "https://domain.com/called/after/processing/request";
options.hasHeaderRow = true;            // If this is `true` the first row is considered as table headers


ZeroBounce::getInstance()->scoringSendFile(
    filePath,
    emailAddressColumn,
    options,
    [](ZBSendFileResponse response) {
        cout << response.toString() << endl;
        scoringFileId = response.fileId;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);
```

* ####### The scoringGetFile API allows users to get the validation results file for the file been submitted using scoringSendfile API
```cpp
std::string fileId = "<FILE_ID>";                       	// The returned file ID when calling scoringSendfile API
std::string localDownloadPath = "<FILE_DOWNLOAD_PATH>";  	// The location where the downloaded file will be saved

ZeroBounce::getInstance()->scoringGetFile(
    fileId,
    localDownloadPath,
    [](ZBGetFileResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);
```

* ####### Check the status of a file uploaded via "scoringSendFile" method
```cpp
std::string fileId = "<FILE_ID>";                    // The returned file ID when calling scoringSendfile API

ZeroBounce::getInstance()->scoringFileStatus(
    fileId,
    [](ZBFileStatusResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);
```

* ####### Deletes the file that was submitted using scoring scoringSendfile API. File can be deleted only when its status is _`Complete`_
```cpp
std::string fileId = "<FILE_ID>";                       // The returned file ID when calling scoringSendfile API

ZeroBounce::getInstance()->scoringDeleteFile(
    scoringFileId,
    [](ZBDeleteFileResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);
```

* #### Email finder allows you to identify the correct email format when you provide a name and email domain or company name
```cpp
ZeroBounce::getInstance()->findEmailByDomain(
    domain,
    first_name,
    middle_name,
    last_name,
    [](ZBFindEmailResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);

ZeroBounce::getInstance()->findEmailByCompanyName(
    company_name,
    first_name,
    middle_name,
    last_name,
    [](ZBFindEmailResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);
```

* #### Domain search allows you to identify the email domain when you provide a domain or company name
```cpp
ZeroBounce::getInstance()->searchDomainByDomain(
    domain,
    [](ZBDomainSearchResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);

ZeroBounce::getInstance()->searchDomainByCompanyName(
    company_name,
    [](ZBDomainSearchResponse response) {
        cout << response.toString() << endl;
    },
    [](ZBErrorResponse errorResponse) {
        cout << errorResponse.toString() << endl;
    }
);
```

#### DEVELOPMENT

It is recommended that the development should be done on Linux.

After checking out the repo, build the library then run tests.

```bash
./build/bin/ZeroBounceTests
```

You should see an output like this

```bash
[==========] 26 tests from 1 test suite ran. (7 ms total)
[  PASSED  ] 26 tests.
```
