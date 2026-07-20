#pragma once
// asvJSON++ v1.7.0 - Main entry point
// Includes core + all formats by default.
// Define ASVJSON_DISABLE_MSG_PACK, ASVJSON_DISABLE_CBOR, ASVJSON_DISABLE_BSON,
// ASVJSON_DISABLE_TOON, ASVJSON_DISABLE_TRON, ASVJSON_DISABLE_GOON,
// ASVJSON_DISABLE_XML, ASVJSON_DISABLE_YAML, ASVJSON_DISABLE_CSV, ASVJSON_DISABLE_TOML
// before including this header to exclude specific formats.

#include "core.hpp"

#ifndef ASVJSON_DISABLE_MSG_PACK
#include "formats/msgpack.hpp"
#endif

#ifndef ASVJSON_DISABLE_CBOR
#include "formats/cbor.hpp"
#endif

#ifndef ASVJSON_DISABLE_BSON
#include "formats/bson.hpp"
#endif

#ifndef ASVJSON_DISABLE_TOON
#include "formats/toon.hpp"
#endif

#ifndef ASVJSON_DISABLE_TRON
#include "formats/tron.hpp"
#endif

#ifndef ASVJSON_DISABLE_GOON
#include "formats/goon.hpp"
#endif

#ifndef ASVJSON_DISABLE_XML
#include "formats/xml.hpp"
#endif

#ifndef ASVJSON_DISABLE_YAML
#include "formats/yaml.hpp"
#endif

#ifndef ASVJSON_DISABLE_CSV
#include "formats/csv.hpp"
#endif

#ifndef ASVJSON_DISABLE_PROTOBUF
#include "formats/protobuf.hpp"
#endif

#ifndef ASVJSON_DISABLE_TOML
#include "formats/toml.hpp"
#endif

#ifndef ASVJSON_DISABLE_SEXPR
#include "formats/sexpr.hpp"
#endif

#ifndef ASVJSON_DISABLE_JSON5
#include "formats/json5.hpp"
#endif
