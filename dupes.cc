/*
 * It's ugly, but it works.
 * It's a fdupes-like utility packed in a single file with
 * standard (stdc++fs) only external libraries required.
 *
 * Use the following command to build it:
 * g++ -Wall -O3 -march=native -std=c++17 dupes.cc -lstdc++fs -o dupes
 *
 * Donate to your local Church.
 * License: LGPL
 */

// TODO: add zero-padding for filesize digits
// TODO: add delete promt option
// TODO: add pretty k/M/G bytes input

#include <cassert>
#include <cmath>
#include <cwchar>   // wchar_t type
#include <cctype>
#include <fcntl.h>  // tty console control
#include <unistd.h> // isatty function

// #include <codecvt>
#include <iomanip>
#include <iostream>
#include <locale>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>

#include <map>
// #include <ranges>   // not available in c++17
#include <unordered_map>
#include <utility>
#include <set>
#include <vector>

#include <algorithm>
#include <chrono>
#include <limits>
#include <regex>
#include <typeinfo>
#include <bits/basic_string.h>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;   // filesystem alias

/* *************************************************************** */
enum {
    ERR_OK = 0,
    ERR_GENERAL = -1,
    ERR_UNKNOWN = -2,
    ERR_HELPMSG_CALLED = -3,
    ERR_DOES_NOT_EXIST = -4,
    ERR_COULD_NOT_OPEN = -5,
    ERR_ARG_INCORRECT = -6
};

/* *************************************************************** */
// std function appending
namespace std {
template<> struct hash<std::pair<uint64_t, uint64_t>> {
    size_t operator() (const std::pair<uint64_t, uint64_t> pair) const {
        uint64_t result = /*0x9e3779b9*/ 0xc70f6907UL;
        const size_t a = std::hash<uint64_t>{} (pair.first);
        const size_t b = std::hash<uint64_t>{} (pair.second);
        return result ^ (a ^ b);
    }
};
}

/* *************************************************************** */
/*                        VARIOUS UTILITIES                        */
/* *************************************************************** */
namespace util {

//void exit(const int code) {

//}

template <typename NUM = uint64_t>
// std::string xdivn(const NUM input, const double div = 1E3) {
std::string xdivn(const NUM input, const double div = 1E3,
        std::vector<std::string> var = { "none", "%.0f", "%.1fK",
                "%.1fM", "%.1fG", "%.1fT", "%.1fP", "%.1fE" } ) {
    assert(div > 1.1);
    double x = std::abs(static_cast<double>(input));
    std::string s(32, '\0');
    
    size_t count = 0;
    for (; x > (div-0.1); x /= div) ++count;
    // std::cerr << "\n[" << x << "\t" << div << "\t" << count << "]\t";
    size_t len = std::snprintf(&s[0], s.size(), var[count+1].c_str(), x);
    
    s.resize(len);
    return s;
}

bool _wprint_set = false;

template <typename ...ARGS> void wprint(const wchar_t arg[], ARGS... args) {
    wchar_t buffer[4 * 1024];
    size_t buflen = sizeof(buffer) / sizeof(buffer[0]);
#ifdef _WIN32
    snwprintf(buffer, buflen, arg, args...);
    std::setlocale(LC_ALL, ".1200");
    _setmode(_fileno(stdout), _O_WTEXT);
    std::wprintf(buffer);   // using that breaks std::cout forever
    _wprint_set = true;
#else
    swprintf(buffer, buflen, arg, args...); // unix name for snwprintf
    std::wcout << buffer;   // unix systems are not retarded ^
#endif  // _WIN32
}

template <typename ...ARGS> void wprint(const char arg[], ARGS... args) {
    char buffer[4 * 1024];
    std::snprintf(buffer, sizeof(buffer), arg, args...);
#ifdef _WIN32
    if (_wprint_set) {
        // TODO: resolve additional win32 wprint problem
        std::setlocale(LC_ALL, "");
        _setmode(_fileno(stdout), _O_TEXT);
        _wprint_set = false;
    }
#endif  // _WIN32
    std::printf(buffer);
}

// error stream priting wrapper
template <typename ...ARGS> void eprint(const char arg[], ARGS... args) {
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer), arg, args...);
    std::cerr << buffer;
    // can be replaced with a buffered clog output
}

/* *************************************************************** */
template <typename ARRTYPE>
uint64_t shasher(const ARRTYPE data[], const size_t len = 1) {
    if (data == nullptr) return 0;

    std::hash<ARRTYPE> hasher;
    uint64_t result = /*0x9e3779b9*/ 0xc70f6907UL;
    for (size_t i = 0; i < len; i++) {
        result = (result << 1) ^ hasher(data[i]);
    }
    return result;
}

uint64_t fhasher(const fs::path &fpath) {
    uint64_t seed = 0;
    if (!fs::exists(fpath)) {
        util::eprint("![%s] does not exist\n", fpath.c_str());
    } else {
        std::basic_ifstream<char> st(fpath.c_str(), std::ios::binary | std::ios::in);
        if (!st.is_open()) {
            // fucking win32 encoding problems
            util::eprint("!could not open [%s]\n", fpath.c_str());
            // std::exit(ERR_COULD_NOT_OPEN);
        } else {
            char chars[(size_t) 1E6 + 1];
            while (size_t bytes = st.readsome(chars, 1E6)) {
                seed ^= shasher<char> (chars, bytes);
            }
        }
    }
    return seed;
}

};  // namespace util


/* *************************************************************** */
/*                    ARGUMENT OPTIONS PARSING                     */
/* *************************************************************** */
namespace opts {

template <typename DATA>
struct _opt_st_ {
    char ckey;
    std::string skey;
    std::string desc;
    DATA val /*= static_cast<DATA>(value)*/;
    std::string help;
    bool check = false;
};

enum class opt {
    help = 1,
    // boolean option
    summarize,
    abspath, delete_, human, noempty, nohidden, quiet,
    recursive, sameline, size_show, symlink, verbose, unix_,
    // long (numeric) options
    level, size_max, size_min,
    // list (string) options
    ignored
};

// what is faster to process: a map or a vector?
std::map<opts::opt, struct _opt_st_<std::string>> helps;
std::map<opts::opt, struct _opt_st_<bool>> bools;
std::map<opts::opt, struct _opt_st_<uint64_t>> longs;
std::map<opts::opt, struct _opt_st_<std::vector<std::string>>> lists;

/* *************************************************************** */
void init() {
    helps[opt::help] = { 'H', "help", "a help message",
                "USAGE:\t\tdupes [-options] DUPES_DIRECTORY\n",

                "EXAMPLE:"
                "\tdupes --sameline -l 3 a/ b/\n"
                "OUTPUT:"
                "\t\t>> indexed: 713 files occupying 12.1K bytes\n"
                "\t\t>> hashed: 60 files occupying 357 bytes\n"
                "\t\t>> filtered: 5 files occupying 95 bytes\n"
                "\t\t\n"
                "\t\t25 \"a/aa/aac/f11\" \"a/ac/f12\" \"a/f10\"\n"
                "\t\t10 \"a/ab/abb/f15\" \"b/f15\"\n" };
    
    bools[opt::abspath] = { 'a', "absolute", "print an absolute path to file", false,
                "prints an absolute path to the duplicate file instead\n\t\t"
                "of a relative path to file that is used by default" };
    bools[opt::delete_] = { 'd', "delete", "promt a user which file to keep", false,
                "promts a user to select which files to keep after\n\t\t"
                "a program finds all the duplicate files;\n\t\t"
                "possible options are: preserve all, none, some, etc" };
    bools[opt::human] = { 'h', "human", "human-readable file size", false,
                "enables human-readable file size output;\n\t\t"
                "by default there is 1024 bytes in a kilobyte" };
    bools[opt::noempty] = { 'n', "noempty", "omit 0-length files", false,
                "if enabled will omit files with 0 filesize length\n\t\t"
                "from consideration;\n\t\t"
                "this option is superceeded by --size-min 2 (and +);\n\t\t"
                "this option exists solely as a compatibility layer" };
    bools[opt::nohidden] = { 'N', "nohidden", "omit hidden files and directories", false,
                "if a file or a directory name starts with a \'.\' symbol\n\t\t"
                "a program considers it to be hidden and omits it from\n\t\t"
                "the consideration;\n\t\t"
                "currently there is no easy way to provide portable way\n\t\t"
                "to differentiate between hidden and non-hidden files on\n\t\t"
                "a variety of operating systems (primarily on windows)" };
    bools[opt::quiet] = { 'q', "quiet", "suppress additional information", false,
                "suppresses typical information output during execution;\n\t\t"
                "enabling this mode will suppress verbose output" };
    bools[opt::recursive] = { 'r', "recursive", "recursive search in sub-dirs", false,
                "enables recursive in-depth search in sub-directories;\n\t\t"
                "can be limited to a certain depth level with a --level option;\n\t\t"
                "setting --level to 1 and bigger automatically enables recursive mode" };
    bools[opt::sameline] = { '1', "sameline", "group dupes on a sameline", false,
                "will group duplicate files on a single line if enabled" };
    bools[opt::size_show] = { 'S', "size-show", "print dupes filesize", false,
                "prints a filesize for a group of dupes during output;\n\t\t"
                "if a --sameline option is specified will print a filesize\n\t\t"
                "on the same line with a group of dupes, which can ease sorting" };
    bools[opt::summarize] = { 'm', "summarize", "show search summary", false,
                "will only print a summary information upon execution;\n\t\t"
                "a summary info might be muted by a --queit option" };
    bools[opt::symlink] = { 's', "symlink", "try to follow symlinks", false,
                "if enabled will try to follow an encountered symlink\n\t\t"
                "and process the file as normal, even if a symlink\n\t\t"
                "points to a file in an another directory" };
    bools[opt::verbose] = { 'v', "verbose", "verbose printing to stderr", false,
                "enables additional verbose messages to stderr output during\n\t\t"
                "the program execution;\n\t\t--queit option suppresses verbose mode" };
    bools[opt::unix_] = { 'u', "unix", "return posix error code", false,
                "returns posix-like error code upon execution;\n\t\t"
                "by default will return the number of duplicate files found" };

    const uint64_t _longs_max = std::numeric_limits<uint64_t>::max();
    longs[opt::level] = { 'l', "level", "maximum depth of a recursive search", _longs_max,
                "maximum level of a recursive search depth for files in\n\t\t"
                "included sub-directories;\n\t\t"
                "by default starts at 0 which is root directory" };
    longs[opt::size_max] = { 'T', "size-max", "maximum filesize to process", _longs_max,
                "maximum filesize that will be tested for a possible dupe;\n\t\t"
                "accepts a number in an exponential format: \'1.23E4\'" };
    longs[opt::size_min] = { 'F', "size-min", "minimum filesize to process", 0,
                "minimum filesize that will be tested for a possible dupe;\n\t\t"
                "superceeds a simpler --noempty option\n\t\t"
                "accepts a number in an exponential format: \'1.23E4\'" };
                    
    lists[opt::ignored] = { 'i', "ignored", "a list of files to ignore", { },
                "specify a comma separated list of strings, upon finding\n\t\t"
                "of which inside a filepath a file will be ignored" };
}

};  // opts
/* *************************************************************** */
namespace check {
// quick boolean values get (a real c++ namespace retardation)
bool abspath() { return opts::bools.at(opts::opt::abspath).val; }
bool delete_() { return opts::bools.at(opts::opt::delete_).val; }
bool human() { return opts::bools.at(opts::opt::human).val; }
bool noempty() { return opts::bools.at(opts::opt::noempty).val; }
bool nohidden() { return opts::bools.at(opts::opt::nohidden).val; }
bool quiet() { return opts::bools.at(opts::opt::quiet).val; }
bool recursive() { return opts::bools.at(opts::opt::recursive).val; }
bool sameline() { return opts::bools.at(opts::opt::sameline).val; }
bool symlink() { return opts::bools.at(opts::opt::symlink).val; }
bool summary() { return opts::bools.at(opts::opt::summarize).val; }
bool size_show() { return opts::bools.at(opts::opt::size_show).val; }
bool unix() { return opts::bools.at(opts::opt::unix_).val; }
bool verbose() { return !quiet() && opts::bools.at(opts::opt::verbose).val; }
};  // check

/* *************************************************************** */
namespace util {
// wrappers for verbose / quiet / normal printing
template <typename ...ARGS>
void vprint(const char arg[], ARGS... args) { if (check::verbose()) eprint(arg, args...); }
template <typename ...ARGS>
void print(const char arg[], ARGS... args) { if (!check::quiet()) eprint(arg, args...); }

void _print_help() {
    auto _print_separator = [](char symbol = '*', size_t lim = 80) {
        for (size_t i = 0; i < lim; i++) util::eprint("%c", symbol);
        util::eprint("\n");
    };
    
    _print_separator('*', 72);
    util::eprint(opts::helps.at(opts::opt::help).val.c_str());
    _print_separator('*', 72);
    
    for (const auto &[k,v] : opts::bools)
        util::eprint("-%c, --%s\t%s\n\n", v.ckey, v.skey.c_str(), v.help.c_str());
    for (const auto &[k,v] : opts::longs)
        util::eprint("-%c, --%s\t%s\n\n", v.ckey, v.skey.c_str(), v.help.c_str());
    for (const auto &[k,v] : opts::lists)
        util::eprint("-%c, --%s\t%s\n\n", v.ckey, v.skey.c_str(), v.help.c_str());
    
    _print_separator('*', 72);
    util::eprint(opts::helps.at(opts::opt::help).help.c_str());
    _print_separator('*', 72);
    
    // abruptly finish the program execution
    int reason = ERR_HELPMSG_CALLED;
    std::exit((check::unix())? std::abs(reason) : reason);
}

};  // util

/* *************************************************************** */
/*                    ARGUMENT OPTIONS PARSING                     */
/* *************************************************************** */
namespace opts {
    
auto _getopt(const std::string &opt) {
    assert(opt.size() > 0);
    bool longy = false;
    std::string key = opt.substr(1, opt.size() - 1);
    if (key[0] == '-') {
        longy = true;
        key = key.substr(1, opt.size() - 1);
    }
    return std::make_pair(longy, key);
}

/* *************************************************************** */
bool find_opt_help(const std::string &opt) {
    auto [flag,key] = _getopt(opt);
    const auto v = helps.at(opt::help);
    if ( (flag && (v.skey == key)) || (!flag && (v.ckey == key[0])) ) {
        util::vprint("%s option found, exiting!\n", v.desc.c_str());
        return true;
    }
    return false;
}

// void find_opt_bool(const std::string &opt) {
void find_opt_bool(const bool flag, std::string key) {
    // auto [flag,key] = _getopt(opt);
    for (auto &[k,v] : bools) {
        if ( (flag && (v.skey == key)) || (!flag && (v.ckey == key[0])) ) {
            v.check = true;
            v.val = true;
            util::vprint("found option [%s]: %s\n", \
                    ((flag)? key : key.substr(0, 1)).c_str(), v.desc.c_str() );
            return;
        }
    }
}

bool find_opt_long(const bool flag, std::string key, const std::string &next) {
    for (auto &[k,v] : longs) {
        if ( (flag && (v.skey == key)) || (!flag && (v.ckey == key[0])) ) {
            try {
                v.check = true;
                v.val = std::abs(std::round(std::stod(next)));
            } catch (.../*std::invalid_argument e*/) {
                util::eprint("error: incorrect argument [%s] to option [%s]\n",
                            next.c_str(), v.skey.c_str());
                int res = ERR_ARG_INCORRECT;
                std::exit((check::unix())? std::abs(res) : res);
            }
            util::vprint("found option [%s:%llu]: %s\n", \
                    ((flag)? key : key.substr(0, 1)).c_str(),
                    v.val, v.desc.c_str());
            return true;
        }
    }
    return false;
}

//bool find_opt_list(const std::string &opt, const std::string &next) {
bool find_opt_list(const bool flag, std::string key, const std::string &next) {
    assert(next.size() > 0);
    //auto [flag,key] = _getopt(opt);
    for (auto &[k,v] : lists) {
        if ( (flag && (v.skey == key)) || (!flag && (v.ckey == key[0])) ) {
            std::string list = next;    // copy a mutable string
            if ((list[0] == '\'') || (list[0] == '\"')) {
                list = list.substr(1, list.size() - 2);
            }
            v.check = true;
            // parse comma-separated list
            std::stringstream ss(list);
            std::string str;
            while (getline(ss, str, ',')) {
                v.val.push_back(str);
                util::vprint("found option [%s:%s]: %s\n", \
                        ((flag)? key : key.substr(0,1)).c_str(),
                        str.c_str(), v.desc.c_str());
            }
            return true;
        }
    }
    return false;
}

/* *************************************************************** */
std::vector<std::string> dirs;

void parse_opts(int &pos, const int argc, char *argv[]) {
    for (; pos < argc; pos++) {
        std::string opt = argv[pos];
        if ((opt.size() < 2) || (opt[0] != '-')) return;
        
        const auto [flag, str] = _getopt(opt);
        for (size_t i = 0; i < str.size(); i++) {
            opt = str.substr(i, str.size());
            find_opt_bool(flag, opt);
            if (pos < (argc - 1)) { // check that we do not fall off
                std::string next = argv[pos + 1];
                if (find_opt_long(flag, opt, next)) pos++;
                if (find_opt_list(flag, opt, next)) pos++;
            }
            if (flag) continue;     // only 1 pass for a long key
        }
    }
}

void parse_dirs(int &pos, const int argc, char *argv[]) {
    for (; pos < argc; pos++) {
        std::string opt = argv[pos];
        if ((opt[0] == '\'') || (opt[0] == '\"')) {
            opt = opt.substr(1, opt.size() - 2);
        }
        bool unique = true;
        for (const auto &s : dirs) {
            if (s == opt) {
                util::vprint("same entry [%s] found\n", opt.c_str());
                unique = false;
                break;
            }
        }
        if (unique) {
            dirs.push_back(opt);
            util::vprint("<< entry [%s] added\n", opt.c_str());
        }
    }
}

void parse(int argc, char* argv[]) {
    if (argc <= 1) util::_print_help();
    for (int i = 1; i < argc; i++) {
        std::string opt = argv[i];
        if (find_opt_help(opt)) util::_print_help();
    }
    
    // remember iterator location
    int argc_iter = 1;
    parse_opts(argc_iter, argc, argv);
    parse_dirs(argc_iter, argc, argv);
}

};  // namespace opts


/* *************************************************************** */
/*                      RUNTIME OPTIONS CHECK                      */
/* *************************************************************** */
namespace check {

bool fperms(const fs::path &fpath) {
    auto perm = fs::status(fpath).permissions();
    return ((perm & fs::perms::group_read) != fs::perms::none);
}

bool fsymlink(const fs::path &fpath) {
    auto type = fs::status(fpath).type();
    bool flag = check::symlink();
    return flag && (type == fs::file_type::symlink);
}

bool fregular(fs::path &fpath) {
    auto type = fs::status(fpath).type();
    return (type == fs::file_type::regular);
}

bool fdepth(const fs::recursive_directory_iterator iter) {
    const size_t curr = iter.depth();
    if (curr == 0) return true; // quick zero level check
    const auto sdep = opts::longs.at(opts::opt::level);
    if (sdep.check) return sdep.val >= curr;
    else return check::recursive();
}

bool fhidden(const fs::path &fpath) {
    if (!(check::nohidden())) return true;
    else {
        for (const auto &entry : fpath) {
            std::string val = entry.string();
            if ((val.size() > 1) && (val[0] == '.') && (val[1] != '.')) {
                util::vprint("hidden entry: %-55s\n", val.c_str());
                return false;
            }
        }
        return true;
    }
}

/* *************************************************************** */
bool fsize(const fs::path &fpath) {
    auto type = fs::status(fpath).type();
    if (type != fs::file_type::regular) return false;
    
    size_t min = 0, max = std::numeric_limits<size_t>::max();
    auto smax = opts::longs.at(opts::opt::size_max);
    if (smax.check) max = smax.val;
    
    auto smin = opts::longs.at(opts::opt::size_min);
    if (smin.check) min = smin.val;
    const bool flag = check::noempty();
    if (flag && (min == 0)) min = 1;    // update minimum fsize
    
    size_t fsize = fs::file_size(fpath);
    return (fsize >= min) && (fsize <= max);
}

// map is too slow to be parsed during run-time
bool fignored(const fs::path &fpath) {
    auto list = opts::lists.at(opts::opt::ignored);
    if (!list.check || (list.val.size() == 0)) return true;
    
    std::string path = fpath.string();
    for (const auto &s : list.val) {
        if (path.find(s) != std::string::npos) {
            util::vprint("ignoring: [%s] in %-55s\n",
                         s.c_str(), path.c_str());
            return false;
        }
    }
    return true;
}

};  // namespace check

/* *************************************************************** */
/*                       FILE LIST PROCESSING                      */
/* *************************************************************** */
namespace filelist {

template <typename HASH = uint64_t>
using filemap_t = std::unordered_map<HASH, std::vector<fs::path>>;
filemap_t<size_t> bysize(1024 * 1024);
filemap_t<std::pair<size_t, uint64_t>> byhash2(128 * 1024);
filemap_t<std::pair<uint64_t, uint64_t>> byfinal2(128 * 1024);

std::vector<fs::path> dirlist;

struct count_t {
    uint64_t total = 0;
    uint64_t byname = 0, bysize = 0, byhash = 0;
    uint64_t flen_byname = 0, flen_byhash = 0, flen_byfilter = 0;
    uint64_t flen_max = 0;
} count;

/* *************************************************************** */
void _pputs(const char *name, uint64_t &a, uint64_t &b, uint64_t &c) {
    util::print("%s.. [%s out of %s] files occupying %s bytes%24s",
            name, (check::human())? util::xdivn(a, 1024).c_str() \
                    : std::to_string(a).c_str(), \
            (check::human())? util::xdivn(b, 1024).c_str() \
                    : std::to_string(b).c_str(),
            (check::human())? util::xdivn(c, 1024).c_str() \
                    : std::to_string(c).c_str(), "\r");
}

void _rputs(const char *name, uint64_t a, uint64_t b) {
    util::print(">> %s: %s files occupying %s bytes%24s\n",
            name, (check::human())? util::xdivn(a,1024).c_str() \
                    : std::to_string(a).c_str(), \
            (check::human())? util::xdivn(b,1024).c_str() \
                    : std::to_string(b).c_str(), "");
}

using hrclock = std::chrono::high_resolution_clock;
void _print_timespent(std::chrono::time_point<hrclock> tstart) {
    if (check::quiet()) return;
    
    auto t2 = hrclock::now();
    // auto int_ms = duration_cast<std::chrono::milliseconds> (t2 - tstart);
    std::chrono::duration<double, std::ratio<1, 1>> fp_ms = t2 - tstart;
    util::print("spent %3.3f seconds processing\n", fp_ms.count());
}

/* *************************************************************** */
void append(const std::string &name) {
    fs::path dpath(name);
    if (fs::exists(dpath)) {
        auto type = fs::status(dpath).type();
        if (type == fs::file_type::directory) {
            dpath = dpath.make_preferred();
            dirlist.push_back(dpath);
        } else {
            util::eprint("%s is not a directory\n", name.c_str());
        }
    } else {
        util::eprint("%s does not exist\n", name.c_str());
        // std::cerr << name << " does not exist" << std::endl;
    }
}

void load(const std::vector<std::string> &dirs) {
    for (const auto &dir : dirs) append(dir);
}

/* *************************************************************** */
void build(const fs::path &dirpath) {
    // build a list of files in current directory
    auto iter = fs::recursive_directory_iterator(dirpath);
    for (auto &entry : iter) {
        auto fpath = entry.path();
        if (check::fsymlink(fpath)) fpath = fs::read_symlink(fpath);
        // TODO: check recursive symlinks
        
        if ( check::fregular(fpath) && check::fperms(fpath) \
                && check::fdepth(iter) && check::fhidden(fpath) \
                && check::fsize(fpath) && check::fignored(fpath) ) {
            size_t flen = fs::file_size(fpath);
            bysize[flen].push_back(fpath);
            count.byname++;
            count.flen_byname += flen;
        }
        _pputs("indexing", count.byname, ++count.total, count.flen_byname);
    }
}

void calc(const filemap_t<size_t> &bysize,
          filemap_t<std::pair<size_t, uint64_t>> &byhash) {
    // parse the file using built-in murmurhash and add it to the list
    for (const auto &[key, vals] : bysize) {
        if (vals.size() > 1) {
            for (const auto &f : vals) {
                size_t flen = fs::file_size(f);
                uint64_t fhash = util::fhasher(f);
                byhash[{ flen, fhash }].push_back(f);
                count.bysize++;
                count.flen_byhash += flen;
            }
        }
        _pputs("hashing", count.bysize, count.byname, count.flen_byhash);
    }
}

void filter(const filemap_t<std::pair<size_t, uint64_t>> &byhash,
        filemap_t<std::pair<size_t, uint64_t>> &byfinal) {
    // filter vectors with just one element
    for (const auto &[k,vals] : byhash) {
        const uint64_t flen = k.first, fhash = k.second;
        if ((fhash == 0) && (flen != 0)) continue;   // ignore invalid hash
        if (vals.size() > 1) {
            for (const auto &p : vals) {
                byfinal[{flen, fhash}].push_back(p);
                count.byhash++;
                count.flen_byfilter += flen;
                if (flen > count.flen_max) count.flen_max = flen;
            }
        }
        _pputs("filtering", count.byname, count.bysize, count.flen_byfilter);
    }
}

/* *************************************************************** */
std::set<int> prompt_process(const std::string &s, const int maxlen) {
    std::set<int> list;
    if ((s.size() < 1) || (maxlen < 1)) return list;
    
    std::string cut = "";
    std::string vs = "0123456789";
    for (const auto c : s) {
        if (std::any_of(vs.begin(), vs.end(), [c](char v){ return c == v; })) {
            cut.push_back(c);      // appends a digit
        } else {
            cut.push_back(' ');    // empty space
        }
    }
    
    // append digits into a set
    if (s.find("none") != std::string::npos) {
        // fall through to the very end
    } else if (s.find("all") != std::string::npos) {
        for (int i = 1; i <= maxlen; i++) list.insert(i);
    } else {
        // std::string cut = s;
        for (size_t p = 0; p != cut.size(); ) {
            cut = cut.substr(p, cut.size());
            const int i = std::abs(std::stoi(cut, &p));
            if ((i > 0) && (i <= maxlen)) list.insert(i);
        }
    }
    return list;
}

std::set<int> prompt_invert(const std::set<int> &input, const int maxlen) {
    std::set<int> out;
    for (int i = 1; i <= maxlen; i++) {
        if (input.find(i) == input.end()) out.insert(i);
    }
    return out;
}

void prompt_delete(const std::vector<fs::path> &vals) {
    if (!isatty(fileno(stdout))) return;
    if (!check::delete_()) return;
    if (vals.size() < 2) return;

    std::string si;
    util::wprint("Choose which files to keep (all/1/2,3/none):\n");
    // std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::getline(std::cin, si);
    auto vi = prompt_process(si, vals.size());
    vi = prompt_invert(vi, vals.size());
    for (int v : vi) {
        util::wprint("removing [%d] ", v);
        util::wprint(vals[v-1].c_str());
        util::wprint("\n");
        fs::remove(vals[v-1]);
    }
}

/* *************************************************************** */
void _print_res_normal() {
    for (const auto &[k,vals] : byfinal2) {
        const uint64_t flen = k.first, fhash = k.second;
        if ((fhash == 0) && (flen != 0)) continue;   // ignore invalid hash

        if (check::size_show()) {
            util::wprint("%s%s", ((!check::human())?
                    std::to_string(flen) : util::xdivn(flen, 1024)).c_str(),
                    (check::sameline())? " " : "\n");
        }
        for (const auto &p : vals) {
            auto path = (check::abspath())? fs::canonical(p) : p;
            util::wprint("\"%s\"", path.string().c_str());
            util::wprint("%s", (check::sameline())? " " : "\n");
        }
        prompt_delete(vals);
        util::wprint("\n");
    }
}

void _print_res_win32() {
    for (const auto &[k,vals] : byfinal2) {
        const uint64_t flen = k.first, fhash = k.second;
        if ((fhash == 0) && (flen != 0)) continue;   // ignore invalid hash

        if (check::size_show()) {
            if (!check::human()) {
                util::wprint(L"%llu", flen);
            } else {
                std::string xdiv = util::xdivn(flen, 1024);
                std::wstring wxdiv { xdiv.begin(), xdiv.end() };
                util::wprint(wxdiv.c_str());
            }
            util::wprint((check::sameline())? L" " : L"\n");
        }
        
        size_t count = 1;
        for (const auto &p : vals) {
            if (check::delete_()) {
                util::wprint((std::to_wstring(count++)).c_str());
                util::wprint(") ");
            }
            util::wprint(L"\"");
            util::wprint(((check::abspath())? fs::canonical(p) : p).c_str());
            util::wprint(L"\"");
            util::wprint((check::sameline())? L" " : L"\n");
            if (check::sameline() && check::delete_()) util::wprint(L"\n");
        }
        prompt_delete(vals);
        util::wprint(L"\n");
    }
}

/* *************************************************************** */
void _print_res(){
    // windows boilerplate
    const bool win32 = (sizeof(fs::path::value_type) == 2);
    
    if ((win32) && (isatty(fileno(stdout)))) {
        if (byfinal2.size() > 0) util::print("\n");
        _print_res_win32();
    } else {
        _print_res_normal();
    }
}

/* *************************************************************** */
int process() {
    auto tstart = hrclock::now();
    for (const auto &dir : dirlist) build(dir);
    _rputs("indexed", count.byname, count.flen_byname);
    calc(bysize, byhash2);
    _rputs("hashed", count.bysize, count.flen_byhash);
    filter(byhash2, byfinal2);
    _rputs("filtered", count.byhash, count.flen_byfilter);
    
    if (!check::summary()) {
        _print_timespent(tstart);
        _print_res();
    }
    return (check::unix())? ERR_OK : static_cast<int>(count.byhash);
}

};  // namespace filelist

/* *************************************************************** */
/*                    PROGRAM MAIN ENTRY POINT                     */
/* *************************************************************** */
int main(int argc, char *argv[]) {
//    return filelist::test();
    opts::init();
    opts::parse(argc, argv);
    filelist::load(opts::dirs);
    return filelist::process();
}
