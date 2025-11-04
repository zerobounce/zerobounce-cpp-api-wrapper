#ifndef ZBAPIURL_H
#define ZBAPIURL_H

#include <string>

/**
 * Enum that lists all the possible API base URLs.
 */
enum ZBApiURL {
    Default,
    USA,
    EU
};

/**
 * @brief Function used to return base url string corresponding to a ZBApiURL.
 *
 * @param zbApiUrl ZBApiURL enum value
 * @return string corresponding base url string
 */
std::string baseURLStringFromZBApiURL(ZBApiURL zbApiUrl);

#endif