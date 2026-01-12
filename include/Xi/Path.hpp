#ifndef XI_PATH_HPP
#define XI_PATH_HPP 1

#include <cstdio>
#include "String.hpp"
#include "Array.hpp"
#include "Map.hpp"

namespace Xi
{

    class Path
    {
    private:
        String _protocol;
        String _hostname;
        String _port;
        Array<String> _segments;
        bool _isAbsolute; // Track if path is absolute (starts with /)

        // Query handling
        mutable Map<String, String> _queryMap;
        mutable bool _queryParsed;
        mutable String _rawQuery;

        // -------------------------------------------------------------------------
        // Helpers
        // -------------------------------------------------------------------------

        static String urlDecode(const String &in)
        {
            String out;
            const u8 *d = in.data();
            usz len = in.length;
            for (usz i = 0; i < len; ++i)
            {
                if (d[i] == '%' && i + 2 < len)
                {
                    auto hexVal = [](u8 c) -> int
                    {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return 0;
                    };
                    out.push((u8)((hexVal(d[i + 1]) << 4) | hexVal(d[i + 2])));
                    i += 2;
                }
                else if (d[i] == '+') out.push(' ');
                else out.push(d[i]);
            }
            return out;
        }

        void parseQuery() const
        {
            if (_queryParsed) return;
            const_cast<Map<String, String>&>(_queryMap).clear();

            if (_rawQuery.length == 0) {
                _queryParsed = true;
                return;
            }

            Array<String> pairs = _rawQuery.split("&");
            for (usz i = 0; i < pairs.length; i++)
            {
                String &pair = pairs[i];
                long long eq = pair.find("=");
                if (eq != -1) {
                    String k = urlDecode(pair.begin(0, (usz)eq));
                    String v = urlDecode(pair.begin((usz)eq + 1, pair.length));
                    _queryMap.put(k, v);
                } else {
                    _queryMap.put(urlDecode(pair), "");
                }
            }
            _queryParsed = true;
        }

        // Optimized mergePath that avoids String::replace/split bugs
        void mergePath(const String &rawPath, bool resetStack)
        {
            if (resetStack) _segments = Array<String>();

            const u8* data = rawPath.data();
            usz len = rawPath.length;
            usz start = 0;

            for(usz i = 0; i < len; ++i) {
                char c = (char)data[i];
                if (c == '/' || c == '\\') {
                    if (i > start) {
                        String seg = ((Array<u8>)rawPath).begin(start, i);
                        processSegment(seg);
                    }
                    start = i + 1;
                }
            }
            
            // Handle tail segment
            if (start < len) {
                String seg = ((Array<u8>)rawPath).begin(start, len);
                processSegment(seg);
            }
        }

        void processSegment(const String &p) {
            if (p.length == 0 || p == ".") return;
            if (p == "..") {
                if (_segments.length > 0) _segments.pop();
            } else {
                _segments.push(p);
            }
        }

    public:
        // -------------------------------------------------------------------------
        // Constructors
        // -------------------------------------------------------------------------
        Path() : _isAbsolute(false), _queryParsed(false) {}

        Path(const String &path) : _isAbsolute(false), _queryParsed(false)
        {
            resolve(true, path);
        }

        Path(const Array<String> &paths) : _isAbsolute(false), _queryParsed(false)
        {
            if (paths.length == 0) return;
            for (usz i = 0; i < paths.length; ++i)
            {
                resolve((i == 0), paths[i]);
            }
        }

    private:
        void resolve(bool isLeader, const String &raw)
        {
            if (raw.length == 0) return;

            // 1. Extract Query
            String pathPart = raw;
            long long qIdx = raw.find("?");
            if (qIdx != -1)
            {
                pathPart = ((Array<u8>)raw).begin(0, (usz)qIdx);
                String q = ((Array<u8>)raw).begin((usz)qIdx + 1, raw.length);
                if (_rawQuery.length > 0) _rawQuery += "&";
                _rawQuery += q;
                _queryParsed = false;
            }

            // 2. Extract Protocol
            long long protoIdx = pathPart.find("://");
            usz pathStart = 0;

            if (protoIdx != -1)
            {
                _isAbsolute = true; // Protocol implies absolute
                if (isLeader)
                {
                    _protocol = pathPart.begin(0, (usz)protoIdx);
                    usz afterProto = (usz)protoIdx + 3;
                    long long pathSlash = pathPart.find("/", afterProto);
                    usz hostEnd = (pathSlash == -1) ? pathPart.length : (usz)pathSlash;

                    String auth = pathPart.begin(afterProto, hostEnd);
                    long long portColon = auth.find(":");
                    if (portColon != -1) {
                        _hostname = auth.begin(0, (usz)portColon);
                        _port = auth.begin((usz)portColon + 1, auth.length);
                    } else {
                        _hostname = auth;
                        _port = "";
                    }
                    pathStart = hostEnd;
                }
                else
                {
                    // Protocol found in later part -> Reset (absolute override)
                    usz afterProto = (usz)protoIdx + 3;
                    long long pathSlash = pathPart.find("/", afterProto);
                    pathStart = (pathSlash != -1) ? (usz)pathSlash : pathPart.length;
                    _segments = Array<String>();
                }
            }

            // 3. Process Path
            String p = pathPart.begin(pathStart, pathPart.length);

            // Check for absolute path char
            const u8 *pData = const_cast<String &>(p).data();
            bool isAbsLocal = false;
            if (p.length > 0 && (pData[0] == '/' || pData[0] == '\\')) isAbsLocal = true;
            if (p.length >= 3 && pData[1] == ':' && (pData[2] == '/' || pData[2] == '\\')) isAbsLocal = true;

            if (isAbsLocal) {
                _segments = Array<String>();
                _isAbsolute = true;
            }

            mergePath(p, false);
        }

    public:
        // -------------------------------------------------------------------------
        // Properties
        // -------------------------------------------------------------------------

        String protocol() const { return _protocol; }
        String hostname() const { return _hostname; }
        String port() const { return _port; }

        String host() const
        {
            if (_port.length > 0) return _hostname + ":" + _port;
            return _hostname;
        }

        String basename() const
        {
            if (_segments.length == 0) return "";
            return _segments[_segments.length - 1];
        }

        Map<String, String> &query()
        {
            parseQuery();
            return _queryMap;
        }

        // -------------------------------------------------------------------------
        // Methods
        // -------------------------------------------------------------------------

        String toString(bool forwardSlash = true, bool protocolAndHostname = true, bool withQuery = true) const
        {
            String out;

            if (protocolAndHostname && _protocol.length > 0)
            {
                out += _protocol;
                out += "://";
                out += _hostname;
                if (_port.length > 0) {
                    out += ":";
                    out += _port;
                }
            }

            const char *sep = forwardSlash ? "/" : "\\";
            
            // Logic for leading slash
            bool addLeadingSlash = false;
            if (protocolAndHostname && _protocol.length > 0) {
                 // URL: add slash if we have path segments
                 if (_segments.length > 0) addLeadingSlash = true;
            } else {
                 // FS: add slash if absolute
                 if (_isAbsolute) addLeadingSlash = true;
            }

            if (addLeadingSlash) out += "/";

            for (usz i = 0; i < _segments.length; ++i)
            {
                if (i > 0) out += sep;
                out += _segments[i];
            }

            if (withQuery)
            {
                if (_queryParsed) {
                    if (_queryMap.size() > 0) {
                        out += "?";
                        bool first = true;
                        for (auto it = _queryMap.begin(); it != _queryMap.end(); ++it) {
                            if (!first) out += "&";
                            out += it->key;
                            if (it->value.length > 0) {
                                out += "=";
                                out += it->value;
                            }
                            first = false;
                        }
                    }
                } else if (_rawQuery.length > 0) {
                    out += "?";
                    out += _rawQuery;
                }
            }
            return out;
        }

        String relativeTo(const Path &parent) const
        {
            if (_protocol != parent._protocol || _hostname != parent._hostname || _port != parent._port)
            {
                return "";
            }

            usz common = 0;
            usz minLen = (_segments.length < parent._segments.length) ? _segments.length : parent._segments.length;

            while (common < minLen && _segments[common] == parent._segments[common])
            {
                common++;
            }

            String res;

            usz up = parent._segments.length - common;
            for (usz i = 0; i < up; ++i)
            {
                if (res.length > 0) res += "/";
                res += "..";
            }

            for (usz i = common; i < _segments.length; ++i)
            {
                if (res.length > 0 || up > 0) res += "/";
                res += _segments[i];
            }

            return res;
        }
    };

} // namespace Xi

#endif // XI_PATH_HPP