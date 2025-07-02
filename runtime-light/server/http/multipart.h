#include <string_view>

#include "runtime-common/core/runtime-core.h"

void parse_multipart(const std::string_view &body, const std::string_view &boundary, mixed &v$_POST);

std::string_view parse_boundary(const std::string_view &content_type);