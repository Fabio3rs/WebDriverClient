#pragma once

#include "Strutils.hpp"
#include <Poco/Crypto/DigestEngine.h>
#include <Poco/DOM/DOMParser.h>
#include <Poco/DOM/DOMWriter.h>
#include <Poco/DOM/Document.h>
#include <Poco/DOM/NamedNodeMap.h>
#include <Poco/DOM/NodeFilter.h>
#include <Poco/DOM/NodeIterator.h>
#include <Poco/DOM/NodeList.h>
#include <Poco/DOM/Text.h>
#include <Poco/Dynamic/Var.h>
#include <Poco/HMACEngine.h>
#include <Poco/JSON/JSON.h>
#include <Poco/JSON/Parser.h>
#include <Poco/UUID.h>
#include <Poco/UUIDGenerator.h>
#include <Poco/XML/Content.h>
#include <Poco/XML/Name.h>
#include <Poco/XML/NamePool.h>
#include <Poco/XML/ParserEngine.h>
#include <Poco/XML/XML.h>
#include <Poco/XML/XMLWriter.h>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
