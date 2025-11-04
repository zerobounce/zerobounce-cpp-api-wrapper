#### INSTALACIÓN
Antes de instalar el envoltorio, asegúrate de tener instalado `CMake` en tu sistema. Puedes descargarlo desde [aquí](https://cmake.org/download/).

Puedes instalarlo utilizando CMake con `FetchContent`. Agrega lo siguiente a tu archivo `CMakeLists.txt`:
```cmake
include(FetchContent)
FetchContent_Declare(
    ZeroBounce
    GIT_REPOSITORY https://github.com/zerobounce/zerobounce-cpp-api-wrapper.git
    GIT_TAG <DESIRED_TAG>
)
FetchContent_MakeAvailable(ZeroBounce)
```

Esto generará el objetivo `ZeroBounce` al que puedes enlazar de la manera habitual:

```cmake
target_link_libraries(nombre_de_tu_objetivo PRIVATE ZeroBounce)
```

Si utilizas Visual Studio Code, puedes construir la biblioteca utilizando las extensiones `CMake` y `CMake Tools`. Después de instalar las extensiones, simplemente abre los comandos con `CTRL + SHIFT + P` y utiliza `CMake: Configure`, seguido de `CMake: Build`.

Alternativamente, también puedes utilizar los comandos de la línea de comandos en el directorio raíz de tu proyecto.

###### LINUX

```bash
mkdir build
cmake -S . -B build -G "Unix Makefiles"
cmake --build build
```

###### WINDOWS

Asegúrate de tener instalado `MinGW` en tu sistema. Puede ser instalado siguiendo los pasos [aquí](https://code.visualstudio.com/docs/cpp/config-mingw).

```bash
mkdir build
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

Ten en cuenta que es posible que la biblioteca requiera algunos archivos .dll (que se encuentran en el directorio `build`) para funcionar correctamente en tu proyecto.

#### USO
Incluye la biblioteca en tu archivo:
```cpp
###include <ZeroBounce/ZeroBounce.h>
```

Inicializa el envoltorio con tu clave de API y tu API preferida:
```cpp
ZeroBounce::getInstance()->initialize("<TU_CLAVE_DE_API>", ZBApiURL::Default);
```

#### Ejemplos
Luego puedes utilizar cualquiera de los métodos del envoltorio, por ejemplo:
* ####### Comprobar cuántos créditos te quedan en tu cuenta
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

* ####### Comprobar el uso de tu API durante un período de tiempo determinado
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

* ####### Validar una dirección de correo electrónico
```cpp
std::string email = "valido@ejemplo.com";   // La dirección de correo electrónico que deseas validar
std::string ipAddress = "127.0.0.1";     // La dirección IP desde la que se registró el correo electrónico (opcional)

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

* ####### Validar un lote de correos electrónicos
```cpp
std::vector<ZBEmailToValidate> emails = {
    {"valido@ejemplo.com", "1.1.1.1"},
    {"invalido@ejemplo.com"}
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

* ####### Utilizar el punto final de la API de actividad para obtener información sobre la participación general de los suscriptores en los correos electrónicos
```cpp
std::string email = "valido@ejemplo.com";    // Dirección de correo electrónico del suscriptor

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

* ####### La API sendfile permite al usuario enviar un archivo para la validación masiva de correos electrónicos
```cpp
std::string filePath = "<RUTA_DEL_ARCHIVO>"; 	// Ruta del archivo csv o txt
int emailAddressColumn = 3; 			// El índice de la columna "email" en el archivo. El índice comienza en 1
SendFileOptions options;			// Opciones adicionales

options.returnUrl = "https://dominio.com/llamado/después/de/procesar/solicitud";
options.firstNameColumn = 4;		// El índice de la columna "nombre" en el archivo
options.lastNameColumn = 5;		// El índice de la columna "apellido" en el archivo
options.genderColumn = 6;		// El índice de la columna "género" en el archivo
options.ipAddressColumn = 7;		// El índice de la columna "dirección IP" en el archivo
options.hasHeaderRow = true;		// Si esto es `true`, la primera fila se considera como encabezados de tabla
options.removeDuplicate = true;	// Si esto es `true`, se eliminarán los correos electrónicos duplicados

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

* ####### La API getfile permite a los usuarios obtener el archivo de resultados de validación para el archivo enviado utilizando la API sendfile
```cpp
std::string fileId = "<ID_DE

_ARCHIVO>";				// El ID de archivo devuelto al llamar a la API sendfile
std::string localDownloadPath = "<RUTA_DE_DESCARGA_DEL_ARCHIVO>"; 	// La ubicación donde se guardará el archivo descargado

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

* ####### Comprobar el estado de un archivo cargado a través del método "sendFile"
```cpp
std::string fileId = "<ID_DE_ARCHIVO>";		// El ID de archivo devuelto al llamar a la API sendfile

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

* ####### Eliminar el archivo que se envió utilizando la API sendfile. El archivo solo se puede eliminar si su estado es _`Completo`_
```cpp
std::string fileId = "<ID_DE_ARCHIVO>";		// El ID de archivo devuelto al llamar a la API sendfile

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

* ####### La API scoringSendfile permite al usuario enviar un archivo para la validación masiva de correos electrónicos
```cpp
std::string filePath = "<RUTA_DEL_ARCHIVO>";		// Ruta del archivo csv o txt
int emailAddressColumn = 3; 			// El índice de la columna "email" en el archivo. El índice comienza en 1
SendFileOptions options;			// Opciones adicionales

options.returnUrl = "https://dominio.com/llamado/después/de/procesar/solicitud";
options.hasHeaderRow = true;            // Si esto es `true`, la primera fila se considera como encabezados de tabla


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

* ####### La API scoringGetFile permite a los usuarios obtener el archivo de resultados de validación para el archivo enviado utilizando la API scoringSendfile
```cpp
std::string fileId = "<ID_DE_ARCHIVO>";                       	// El ID de archivo devuelto al llamar a la API scoringSendfile
std::string localDownloadPath = "<RUTA_DE_DESCARGA_DEL_ARCHIVO>";  	// La ubicación donde se guardará el archivo descargado

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

* ####### Comprobar el estado de un archivo cargado a través del método "scoringSendFile"
```cpp
std::

string fileId = "<ID_DE_ARCHIVO>";                    // El ID de archivo devuelto al llamar a la API scoringSendfile

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

* ####### Eliminar el archivo que se envió utilizando la API scoring scoringSendfile. El archivo solo se puede eliminar si su estado es _`Completo`_
```cpp
std::string fileId = "<ID_DE_ARCHIVO>";                       // El ID de archivo devuelto al llamar a la API scoringSendfile

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

* #### El buscador de correos electrónicos te permite identificar el formato correcto de correo electrónico cuando proporcionas un nombre y un dominio de correo electrónico o el nombre de una empresa.
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

* #### La búsqueda de dominio le permite identificar el dominio de correo electrónico cuando proporciona un dominio o nombre de empresa.
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

#### DESARROLLO

Se recomienda realizar el desarrollo en Linux.

Después de revisar el repositorio, construye la biblioteca y luego ejecuta las pruebas.

```bash
./build/bin/ZeroBounceTests
```

Deberías ver una salida como esta:

```bash
[==========] 26 pruebas desde 1 suite de pruebas se ejecutaron. (7 ms en total)
[  PASSED  ] 26 pruebas.
```
