#include "runtime-light/server/http/multipart.h"

#include <string_view>

#include "runtime-common/core/core-types/decl/string_decl.inl"
#include "runtime-common/core/core-types/decl/mixed_decl.inl"

constexpr std::string_view CONTENT_DISPOSITION_FORM_DATA = "Content-Disposition: form-data;";
constexpr std::string_view MULTIPART_BOUNDARY_EQ = "boundary=";

// Represents one attribute from Content-Desposition header. 
// For example, a typically file field will have two attributes: 
// 1) attr = "name", value = "avatar"
// 2) attr = "filename", value = "my_avatar.png"
struct PartAttr {
  std::string_view attr, value;

  PartAttr() {};
  PartAttr(const std::string_view &attr_, const std::string_view &value_) : attr{attr_}, value{value_} {};
};

// Represents a parser of Content-Desposition header string.
struct ContentDispHeader {
  private:
    std::string_view header;
    size_t pos{0};

  public:
    ContentDispHeader(const std::string_view &header_) : header{header_} {}
    PartAttr next_attr();
    bool end() {
      return pos >= header.size();
    }
  
  private:
    void markEnd() {
      pos = header.size();
    } 
};

PartAttr ContentDispHeader::next_attr() {
    size_t 
      attrStart = header.find_first_not_of(' ', pos),
      attrEnd,
      valStart,
      valEnd;

    if (attrStart == std::string_view::npos) {
      markEnd();
      return PartAttr{};
    }
    
    size_t eqi = header.find('=', attrStart);
    if (eqi == std::string_view::npos) {
      markEnd();
      return PartAttr{};
    }

    attrEnd = eqi-1;
    valStart = eqi+1;
    pos = header.find(";", valStart);

    if (pos == std::string_view::npos) {
      valEnd = header.size()-1;
    } else {
      valEnd = pos-1;
      pos++;
    }
    if (header[valStart] == '"' && header[valEnd] == '"') {
      valStart++;
      valEnd--;
    }

    return PartAttr{header.substr(attrStart, attrEnd-attrStart+1), header.substr(valStart, valEnd-valStart+1)};
}

// Represents one part of multipart content
struct Part {
  std::string_view name, filename, data;
};

struct MultipartBody {  
  private:

    std::string_view body, boundary;
    size_t pos;
  
    std::string_view parse_content_disp_header();
    std::string_view parse_data();
    
    void skip_crlf() {
      if (body[pos] == '\r') {
          pos++;
      }
      if (body[pos] == '\n') {
          pos++;
      }
    }

    void skip_boundary() {
      if (pos == 0) {
        pos += 2;
      }
      pos += boundary.size();
      if (body[pos] == '-' && body[pos+1] == '-') {
          pos += 2;
      }
    }

    void markEnd() {
        pos = body.size();
    }
  
  public:

    MultipartBody(const std::string_view &body_, const std::string_view &boundary_) 
      : body{body_}, boundary{boundary_}, pos{0} {}
    
    Part next_part();
    
    bool end() {
      return pos >= body.size();
    }

};


Part MultipartBody::next_part() {    
  Part part;

  if (pos == 0) {
    skip_boundary();
    skip_crlf();
  }

  std::string_view header = parse_content_disp_header();
  if (header.empty()) {
    markEnd();
    return Part{};
  }

  ContentDispHeader parser{header};
  while (!parser.end()) {
    PartAttr pa = parser.next_attr();
    if (pa.attr.empty()) {
      markEnd();
      return Part{};
    }
    if (pa.attr == "name") {
      part.name = pa.value;
    } else if (pa.attr == "filename") {
      part.filename = pa.value;
    }
  }

  skip_crlf();
  part.data = parse_data();
  skip_boundary();
  skip_crlf();
  return part;
}


std::string_view MultipartBody::parse_data() {
  size_t 
    data_start = pos,
    data_end = pos = body.find(boundary, data_start);

  if (pos == std::string_view::npos) {
    return {};
  }

  if (body[data_end-1] != '-' || body[data_end-2] != '-') {
    return {};
  }
  data_end -= 2;
  if (body[data_end] == '\n') {
      data_end--;
  }
  if (body[data_end] == '\r') {
      data_end--;
  }

  if (data_end > data_start) {
    return body.substr(data_start, data_end-data_start-1);
  }

  return {};

}

std::string_view MultipartBody::parse_content_disp_header() {
  if (body.find(CONTENT_DISPOSITION_FORM_DATA, pos) != pos) {
    return {};
  }
  size_t 
    attrs_start = pos + CONTENT_DISPOSITION_FORM_DATA.size(),
    lf = body.find('\n', attrs_start), 
    header_end = lf-1;
  
  if (lf == std::string_view::npos) {
      return {};
  }
  
  if (body[header_end] == '\r') {
    header_end--;
  }

  pos = lf + 1;
  return body.substr(attrs_start, header_end-attrs_start+1);
}


void parse_multipart(const std::string_view &body, const std::string_view &boundary, mixed &v$_POST) {
    MultipartBody mb{body, boundary};
    while (!mb.end()) {
        Part part = mb.next_part();
        if (part.name.empty()) {
          return;
        }
        
        if (!part.filename.empty()) {
          // TODO: implement $_FILES filling
        } else {
          v$_POST.set_value(string(part.name.data(), part.name.size()), string(part.data.data(), part.data.size()));  
        }
    }
}

std::string_view parse_boundary(const std::string_view &content_type) {
  size_t pos = content_type.find(MULTIPART_BOUNDARY_EQ);
  if (pos != std::string_view::npos) {
    std::string_view res = content_type.substr(pos + MULTIPART_BOUNDARY_EQ.size());
    if (res[0] == '"' && res[res.size()-1] == '"') {
        res = res.substr(1, res.size()-2);
    }
    return res;
  }
  return {};
}