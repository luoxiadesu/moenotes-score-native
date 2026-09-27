/* Keep the upstream parser unchanged. Darwin headers expose sig_t; rename the
 * parser's private typedef only after those system headers have been included. */
#include "moenotes_chart_parser.h"
#include "yyjson.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#define sig_t mns_private_parser_signature_t
#include "../vendor/chart-parser/src/moenotes_chart_parser.c"
#undef sig_t
