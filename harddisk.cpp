#include "harddisk.h"
#include "memory.h"
#include "ioutil.h"
#include "autoconf.h"
#include "state_file.h"
#include <stdexcept>
#include <fstream>
#include <iostream>
#include <cstring>
#include <map>
#include <string_view>
#include <filesystem>
#include <algorithm>
#include <variant>
#include <chrono>

//#define LOCAL_RAM_DEBUG
#define FS_HANDLER_DEBUG 1

namespace fs = std::filesystem;

namespace {

#include "exprom.h"

// Shared with exprom
// constexpr uint32_t handler_SysBase = 0x00;
// constexpr uint32_t handler_DosBase = 0x04;
constexpr uint32_t handler_MsgPort = 0x08;
constexpr uint32_t handler_DosList = 0x0C;
constexpr uint32_t handler_Id      = 0x10;
constexpr uint32_t handler_DevName = 0x14;

constexpr uint32_t IDNAME_RIGIDDISK = 0x5244534B; // 'RDSK'
constexpr uint32_t IDNAME_PARTITION = 0x50415254; // 'PART'
constexpr uint32_t IDNAME_FILESYSHEADER = 0x46534844; // 'FSHD'
constexpr uint32_t IDNAME_LOADSEG = 0x4C534547; // 'LSEG'

constexpr int8_t IOERR_NOCMD = -3;
// constexpr int8_t IOERR_BADLENGTH = -4;
constexpr int8_t IOERR_BADADDRESS = -5;

struct scsi_cmd {
    uint32_t scsi_Data; /* word aligned data for SCSI Data Phase */
    uint32_t scsi_Length; /* even length of Data area */
    uint32_t scsi_Actual; /* actual Data used */
    uint32_t scsi_Command; /* SCSI Command (same options as scsi_Data) */
    uint16_t scsi_CmdLength; /* length of Command */
    uint16_t scsi_CmdActual; /* actual Command used */
    uint8_t scsi_Flags; /* includes intended data direction */
    uint8_t scsi_Status; /* SCSI status of command */
    uint32_t scsi_SenseData; /* sense data: filled if SCSIF_[OLD]AUTOSENSE */
    uint16_t scsi_SenseLength; /* size of scsi_SenseData, also bytes to */
    uint16_t scsi_SenseActual; /* amount actually fetched (0 means no sense) */
};

// struct DosPacket
constexpr uint32_t dp_Link = 0;
constexpr uint32_t dp_Port = 4;
constexpr uint32_t dp_Type = 8;
constexpr uint32_t dp_Res1 = 12;
constexpr uint32_t dp_Res2 = 16;
constexpr uint32_t dp_Arg1 = 20;
constexpr uint32_t dp_Arg2 = 24;
constexpr uint32_t dp_Arg3 = 28;
constexpr uint32_t dp_Arg4 = 32;
constexpr uint32_t dp_Arg5 = 36;
constexpr uint32_t dp_Arg6 = 40;
constexpr uint32_t dp_Arg7 = 44;

// struct FileLock
constexpr uint32_t fl_Link = 0;
constexpr uint32_t fl_Key = 4;
constexpr uint32_t fl_Access = 8;
constexpr uint32_t fl_Task = 12;
constexpr uint32_t fl_Volume = 16;
constexpr uint32_t fl_Sizeof = 20;

constexpr int32_t SHARED_LOCK = -2;
constexpr int32_t EXCLUSIVE_LOCK = -1;

// struct FileHandle
constexpr uint32_t fh_Link     =  0;
constexpr uint32_t fh_Port     =  4;
constexpr uint32_t fh_Type     =  8;
constexpr uint32_t fh_Buf      = 12;
constexpr uint32_t fh_Pos      = 16;
constexpr uint32_t fh_End      = 20;
constexpr uint32_t fh_Funcs    = 24;
constexpr uint32_t fh_Func2    = 28;
constexpr uint32_t fh_Func3    = 32;
constexpr uint32_t fh_Arg1     = 36;
constexpr uint32_t fh_Arg2     = 40;
constexpr uint32_t fh_Sizeof   = 44;

// struct InfoData
constexpr uint32_t id_NumSoftErrors = 0;
constexpr uint32_t id_UnitNumber = 4;
constexpr uint32_t id_DiskState = 8;
constexpr uint32_t id_NumBlocks = 12;
constexpr uint32_t id_NumBlocksUsed = 16;
constexpr uint32_t id_BytesPerBlock = 20;
constexpr uint32_t id_DiskType = 24;
constexpr uint32_t id_VolumeNode = 28;
constexpr uint32_t id_InUse = 32;
constexpr uint32_t id_Sizeof = 36;

// For id_DiskState
constexpr uint32_t ID_WRITE_PROTECTED = 80; // Disk is write protected
constexpr uint32_t ID_VALIDATING = 81; // Disk is currently being validated
constexpr uint32_t ID_VALIDATED = 82; // Disk is consistent and writeable

constexpr uint32_t ID_DOS_DISK = 0x444F5300; // 'DOS\0'

// struct FileInfoBlock
constexpr uint32_t fib_DiskKey = 0; // LONG
constexpr uint32_t fib_DirEntryType = 4; // LONG (< 0 folr plain file, > 0 for directory, ST_xxx)
constexpr uint32_t fib_FileName = 8; // char[108]
constexpr uint32_t fib_Protection = 116; // LONG RWXD in lower 4 bits
constexpr uint32_t fib_EntryType = 120; // LONG
constexpr uint32_t fib_Size = 124; // LONG number of bytes
constexpr uint32_t fib_NumBlocks = 128; // LONG number of blocks in file
constexpr uint32_t fib_Date = 132; // struct DateStamp
constexpr uint32_t fib_Comment = 144; // char[80]
constexpr uint32_t fib_Reserved = 224;
constexpr uint32_t fib_Sizeof = 260;

constexpr int32_t ST_ROOT = 1;
constexpr int32_t ST_USERDIR = 2;
constexpr int32_t ST_FILE = -3;

constexpr uint32_t DOSFALSE = 0;
constexpr uint32_t DOSTRUE = (uint32_t)-1;

struct DosPacket {
    uint32_t dp_Link;
    uint32_t dp_Port;
    uint32_t dp_Type;
    uint32_t dp_Res1;
    uint32_t dp_Res2;
    uint32_t dp_Arg1;
    uint32_t dp_Arg2;
    uint32_t dp_Arg3;
    uint32_t dp_Arg4;
    uint32_t dp_Arg5;
    uint32_t dp_Arg6;
    uint32_t dp_Arg7;
};

#define ACTIONS(X)                  \
    X(ACTION_STARTUP, 0)            \
    X(ACTION_GET_BLOCK, 2)          \
    X(ACTION_SET_MAP, 4)            \
    X(ACTION_DIE, 5)                \
    X(ACTION_EVENT, 6)              \
    X(ACTION_CURRENT_VOLUME, 7)     \
    X(ACTION_LOCATE_OBJECT, 8)      \
    X(ACTION_RENAME_DISK, 9)        \
    X(ACTION_WRITE, 'W')            \
    X(ACTION_READ, 'R')             \
    X(ACTION_FREE_LOCK, 15)         \
    X(ACTION_DELETE_OBJECT, 16)     \
    X(ACTION_RENAME_OBJECT, 17)     \
    X(ACTION_MORE_CACHE, 18)        \
    X(ACTION_COPY_DIR, 19)          \
    X(ACTION_WAIT_CHAR, 20)         \
    X(ACTION_SET_PROTECT, 21)       \
    X(ACTION_CREATE_DIR, 22)        \
    X(ACTION_EXAMINE_OBJECT, 23)    \
    X(ACTION_EXAMINE_NEXT, 24)      \
    X(ACTION_DISK_INFO, 25)         \
    X(ACTION_INFO, 26)              \
    X(ACTION_FLUSH, 27)             \
    X(ACTION_SET_COMMENT, 28)       \
    X(ACTION_PARENT, 29)            \
    X(ACTION_TIMER, 30)             \
    X(ACTION_INHIBIT, 31)           \
    X(ACTION_DISK_TYPE, 32)         \
    X(ACTION_DISK_CHANGE, 33)       \
    X(ACTION_SET_DATE, 34)          \
    X(ACTION_SCREEN_MODE, 994)      \
    X(ACTION_READ_RETURN, 1001)     \
    X(ACTION_WRITE_RETURN, 1002)    \
    X(ACTION_SEEK, 1008)            \
    X(ACTION_FINDUPDATE, 1004)      \
    X(ACTION_FINDINPUT, 1005)       \
    X(ACTION_FINDOUTPUT, 1006)      \
    X(ACTION_END, 1007)             \
    X(ACTION_SET_FILE_SIZE, 1022)   \
    X(ACTION_WRITE_PROTECT, 1023)   \
    X(ACTION_SAME_LOCK, 40)         \
    X(ACTION_CHANGE_SIGNAL, 995)    \
    X(ACTION_FORMAT, 1020)          \
    X(ACTION_MAKE_LINK, 1021)       \
    X(ACTION_READ_LINK, 1024)       \
    X(ACTION_FH_FROM_LOCK, 1026)    \
    X(ACTION_IS_FILESYSTEM, 1027)   \
    X(ACTION_CHANGE_MODE, 1028)     \
    X(ACTION_COPY_DIR_FH, 1030)     \
    X(ACTION_PARENT_FH, 1031)       \
    X(ACTION_EXAMINE_ALL, 1033)     \
    X(ACTION_EXAMINE_FH, 1034)      \
    X(ACTION_LOCK_RECORD, 2008)     \
    X(ACTION_FREE_RECORD, 2009)     \
    X(ACTION_ADD_NOTIFY, 4097)      \
    X(ACTION_REMOVE_NOTIFY, 4098)   \
    X(ACTION_EXAMINE_ALL_END, 1035) \
    X(ACTION_SET_OWNER, 1036)

#define DEF_ACTIONS(name, value) constexpr uint32_t name = value;
ACTIONS(DEF_ACTIONS)
#undef DEF_ACTIONS

std::string action_name(uint32_t action)
{
    switch (action) {
#define CASE_ACTION(name, value) \
    case name:                   \
        return #name;
        ACTIONS(CASE_ACTION)
#undef CASE_ACTION
    }
    return "Unknown ACTION " + std::to_string(action);
}

#define ERRORS(X)                          \
    X(NO_ERROR, 0)                         \
    X(ERROR_NO_FREE_STORE, 103)            \
    X(ERROR_TASK_TABLE_FULL, 105)          \
    X(ERROR_BAD_TEMPLATE, 114)             \
    X(ERROR_BAD_NUMBER, 115)               \
    X(ERROR_REQUIRED_ARG_MISSING, 116)     \
    X(ERROR_KEY_NEEDS_ARG, 117)            \
    X(ERROR_TOO_MANY_ARGS, 118)            \
    X(ERROR_UNMATCHED_QUOTES, 119)         \
    X(ERROR_LINE_TOO_LONG, 120)            \
    X(ERROR_FILE_NOT_OBJECT, 121)          \
    X(ERROR_INVALID_RESIDENT_LIBRARY, 122) \
    X(ERROR_NO_DEFAULT_DIR, 201)           \
    X(ERROR_OBJECT_IN_USE, 202)            \
    X(ERROR_OBJECT_EXISTS, 203)            \
    X(ERROR_DIR_NOT_FOUND, 204)            \
    X(ERROR_OBJECT_NOT_FOUND, 205)         \
    X(ERROR_BAD_STREAM_NAME, 206)          \
    X(ERROR_OBJECT_TOO_LARGE, 207)         \
    X(ERROR_ACTION_NOT_KNOWN, 209)         \
    X(ERROR_INVALID_COMPONENT_NAME, 210)   \
    X(ERROR_INVALID_LOCK, 211)             \
    X(ERROR_OBJECT_WRONG_TYPE, 212)        \
    X(ERROR_DISK_NOT_VALIDATED, 213)       \
    X(ERROR_DISK_WRITE_PROTECTED, 214)     \
    X(ERROR_RENAME_ACROSS_DEVICES, 215)    \
    X(ERROR_DIRECTORY_NOT_EMPTY, 216)      \
    X(ERROR_TOO_MANY_LEVELS, 217)          \
    X(ERROR_DEVICE_NOT_MOUNTED, 218)       \
    X(ERROR_SEEK_ERROR, 219)               \
    X(ERROR_COMMENT_TOO_BIG, 220)          \
    X(ERROR_DISK_FULL, 221)                \
    X(ERROR_DELETE_PROTECTED, 222)         \
    X(ERROR_WRITE_PROTECTED, 223)          \
    X(ERROR_READ_PROTECTED, 224)           \
    X(ERROR_NOT_A_DOS_DISK, 225)           \
    X(ERROR_NO_DISK, 226)                  \
    X(ERROR_NO_MORE_ENTRIES, 232)          \
    X(ERROR_IS_SOFT_LINK, 233)             \
    X(ERROR_OBJECT_LINKED, 234)            \
    X(ERROR_BAD_HUNK, 235)                 \
    X(ERROR_NOT_IMPLEMENTED, 236)          \
    X(ERROR_RECORD_NOT_LOCKED, 240)        \
    X(ERROR_LOCK_COLLISION, 241)           \
    X(ERROR_LOCK_TIMEOUT, 242)             \
    X(ERROR_UNLOCK_ERROR, 243)

#define DEF_ERRORS(name, value) constexpr uint32_t name = value;
ERRORS(DEF_ERRORS)
#undef DEF_ERRORS

std::string error_name(uint32_t error)
{
    switch (error) {
#define CASE_ERROR(name, value) \
    case name:                  \
        return #name;
        ERRORS(CASE_ERROR)
#undef CASE_ERROR
    }
    return "Unknown ERROR " + std::to_string(error);
}

uint32_t BADDR(uint32_t bptr)
{
    assert(bptr < 0x1000000 / 4);
    return bptr << 2;
}

uint32_t BPTR(uint32_t cptr)
{
    assert(cptr < 0x1000000 && !(cptr & 3));
    return cptr >> 2;
}

bool check_structure(const uint8_t* sector, const uint32_t id, uint32_t size_bytes = 256)
{
    if (get_u32(&sector[0]) != id) // ID
        return false;

    if (get_u32(&sector[4]) != size_bytes / 4) // Size of structure in longs
        return false;

    uint32_t csum = 0;
    for (uint32_t offset = 0; offset < size_bytes; offset += 4)
        csum += get_u32(sector + offset);

    return csum == 0;
}

std::string dos_type_string(uint32_t dostype)
{
    std::string s;
    for (int i = 0; i < 4; ++i) {
        uint8_t ch = static_cast<uint8_t>(dostype >> (24 - 8 * i));
        if (ch >= ' ') {
            s += ch;
            continue;
        }
        s += "\\x";
        s += hexstring(ch);
    }
    return s;
}

std::string read_bcpl_string(memory_handler& mem, uint32_t ptr)
{
    std::string s;
    uint8_t len = mem.read_u8(ptr++);
    while (len--)
        s += mem.read_u8(ptr++);
    return s;
}

bool ci_equal(const std::string_view l, const std::string_view r)
{
    if (l.length() != r.length())
        return false;
    auto case_convert = [](char ch) {
        // TODO: "DOS\3" style INTL compare at some point
        return ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch;
    };
    for (size_t i = 0, len = l.length(); i < len; ++i)
        if (case_convert(l[i]) != case_convert(r[i]))
            return false;
    return true;
}

constexpr uint32_t HUNK_HEADER = 1011;
constexpr uint32_t HUNK_CODE = 1001;
constexpr uint32_t HUNK_DATA = 1002;
constexpr uint32_t HUNK_BSS = 1003;
constexpr uint32_t HUNK_RELOC32 = 1004;
constexpr uint32_t HUNK_END = 1010;

constexpr uint32_t max_hunks = 3; // keep in check with expansion rom

struct hunk_reloc {
    uint32_t dst_hunk;
    std::vector<uint32_t> offsets;
};

struct hunk {
    uint32_t flags;
    uint32_t type;
    std::vector<uint8_t> data;
    std::vector<hunk_reloc> relocs;
};

std::vector<hunk> parse_hunk_file(const std::vector<uint8_t>& code)
{
    uint32_t pos = 0;

    auto read = [&]() {
        if (pos + 4 > code.size())
            throw std::runtime_error { "Invalid HUNK file" };
        const auto val = get_u32(&code[pos]);
        pos += 4;
        return val;
    };

    if (code.size() % 4 || code.size() < 32 || read() != HUNK_HEADER || read())
        throw std::runtime_error { "Invalid HUNK file" };

    const uint32_t table_size = read();

    if (table_size == 0 || table_size > max_hunks || read() != 0 || read() != table_size - 1)
        throw std::runtime_error { "Invalid HUNK file" };

    std::vector<hunk> hunks(table_size);

    for (uint32_t i = 0; i < table_size; ++i) {
        hunks[i].flags = read();
    }

    uint32_t idx = 0;
    while (pos < code.size()) {
        const auto hunk_type = read();
        switch (hunk_type) {
        case HUNK_BSS:
        case HUNK_CODE:
        case HUNK_DATA: {
            if (idx >= table_size || hunks[idx].type)
                throw std::runtime_error { "Invalid HUNK file" };
            const auto size = read() * 4;
            if (size > (hunks[idx].flags & 0x3FFFFFFF) * 4)
                throw std::runtime_error { "Invalid HUNK file" };
            if (hunk_type == HUNK_BSS)
                break;
            if (pos + size > code.size())
                throw std::runtime_error { "Invalid HUNK file" };
            hunks[idx].type = hunk_type;
            hunks[idx].data = std::vector<uint8_t>(&code[pos], &code[pos + size]);
            pos += size;
            break;
        }
        case HUNK_RELOC32: {
            if (hunks[idx].type != HUNK_CODE && hunks[idx].type != HUNK_DATA)
                throw std::runtime_error { "Invalid HUNK file" };
            for (;;) {
                hunk_reloc hr;
                const uint32_t cnt = read();
                if (cnt == 0)
                    break;
                hr.dst_hunk = read();
                if (hr.dst_hunk >= table_size)
                    throw std::runtime_error { "Invalid HUNK file" };
                hr.offsets.resize(cnt);
                for (uint32_t i = 0; i < cnt; ++i) {
                    hr.offsets[i] = read();
                    if ((hr.offsets[i] & 1) || hr.offsets[i] + 3 > (hunks[idx].flags & 0x3FFFFFFF) * 4)
                        throw std::runtime_error { "Invalid relocation in HUNK file" };
                }
                hunks[idx].relocs.push_back(std::move(hr));
            }
            break;
        }
        case HUNK_END:
            ++idx;
            break;
        default:
            throw std::runtime_error { "Unsupported hunk type: " + std::to_string(hunk_type) };
        }
    }

    if (idx != table_size)
        throw std::runtime_error { "Invalid HUNK file" };

    return hunks;
}

constexpr uint32_t ticks_per_sec = 50;
constexpr uint32_t ticks_per_min = ticks_per_sec * 60;
constexpr uint32_t mins_per_day = 24 * 60;

const auto file_time_epoch_diff = []() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch() - fs::file_time_type::clock::now().time_since_epoch()).count();
}();

uint64_t ticks_since_amiga_epoch(const fs::file_time_type& t)
{
    // DateStamp epoch is Jan 1, 1978, ticks are 1/50s
    using dur_type = std::chrono::duration<uint64_t, std::ratio<1, ticks_per_sec>>;
    constexpr uint64_t epoch_diff = uint64_t(2922) * mins_per_day * ticks_per_min; // 2992 days between 1978/1/1 and 1970/1/1
    return std::chrono::duration_cast<dur_type>(t.time_since_epoch()).count() + file_time_epoch_diff * ticks_per_sec - epoch_diff;
}

uint32_t translate_error(const std::error_code& ec)
{
    const auto err = ec.default_error_condition().value();
    if (err == ENOENT)
        return ERROR_OBJECT_NOT_FOUND;
    else if (err == ENOTEMPTY)
        return ERROR_DIRECTORY_NOT_EMPTY;
    else if (err == EIO) // Happens with e.g. rename foo foo/bar
        return ERROR_OBJECT_IN_USE;
#if FS_HANDLER_DEBUG > 0
    std::cout << "[HD] Unable to translate error: " << ec << " (" << err << ")\n";
#endif
    return ERROR_WRITE_PROTECTED; //??
}

class filesystem_handler {
public:
    class node {
    public:
        node(node&&) = default;
        node(const node&) = delete;
        node& operator=(const node&) = delete;

        ~node()
        {
            std::erase(parent_.children_, this);
        }

        uint32_t id() const
        {
            return id_;
        }

        node& parent()
        {
            return parent_;
        }

        const fs::path& path() const
        {
            return path_;
        }

        int32_t type() const
        {
            return type_;
        }

        bool lock(int32_t access)
        {
            if (access == EXCLUSIVE_LOCK) {
                if (access_count_)
                    return false;
                access_count_ = -1;
            } else {
                if (access_count_ < 0)
                    return false;
                ++access_count_;
            }
            return true;
        }

        void unlock(int32_t access)
        {
            if (access == EXCLUSIVE_LOCK) {
                assert(access_count_ == -1);
                access_count_ = 0;
            } else {
                assert(access_count_ > 0);
                --access_count_;
            }
        }

    private:
        explicit node(uint32_t id, node* parent, const fs::path& path, int32_t type)
            : id_ { id }
            , parent_ { parent ? *parent : *this }
            , path_ { path }
            , type_ { type }
        {
            assert(id != 0);
            assert((type == ST_ROOT && id == 1) || type == ST_USERDIR || type == ST_FILE);
            assert((&parent_ == this) == (type == ST_ROOT));
            if (parent) {
                assert(parent->type() == ST_USERDIR || parent->type() == ST_ROOT);
                parent->children_.push_back(this);
            }
        }

        const uint32_t id_;
        node& parent_;
        const fs::path path_;
        const int32_t type_;
        int access_count_ = 0; // -1 -> exclusive access, 0 no access, > 0 number of shared accesses
        std::vector<node*> children_;

        friend filesystem_handler;
    };

    class file_handle {
    public:
        ~file_handle()
        {
            node_.unlock(access_);
        }

        file_handle(const file_handle&) = delete;
        file_handle& operator=(const file_handle&) = delete;

        node& file_node()
        {
            return node_;
        }

        std::fstream& file()
        {
            return file_;
        }

    private:
        // node 'n' must be locked with 'access' already
        explicit file_handle(node& n, std::fstream&& file, int32_t access)
            : node_ { n }
            , file_ { std::move(file) }
            , access_ { access }
        {
        }

        node& node_;
        std::fstream file_;
        const int32_t access_;

        friend filesystem_handler;
    };

    explicit filesystem_handler(const fs::path& base_dir)
        : base_dir_ { absolute(base_dir) }
        , root_node_ { make_node(nullptr, base_dir_, ST_ROOT) }
    {
        if (!is_directory(base_dir_))
            throw std::runtime_error { base_dir_.string() + " is not a valid directory" };
#if FS_HANDLER_DEBUG > 0
        std::cout << "[HD] File system handler starting with basedir=\"" << base_dir_ << "\"\n";
#endif
    }

    void init(uint32_t msg_port, uint32_t dos_list)
    {
        assert(msg_port_ == 0 && dos_list_ == 0);
        assert(msg_port && dos_list);
        msg_port_ = msg_port;
        dos_list_ = dos_list;
    }

    uint32_t msg_port_address() const
    {
        assert(msg_port_);
        return msg_port_;
    }

    uint32_t dos_list_address() const
    {
        assert(dos_list_);
        return dos_list_;
    }

    node& root_node()
    {
        return root_node_;
    }

    node* node_from_key(uint32_t key)
    {
        auto it = nodes_.find(key);
        if (it == nodes_.end()) {
#if FS_HANDLER_DEBUG > 0
            std::cerr << "[HD] FS handler key $" << hexfmt(key) << " not found\n";
#endif
            return nullptr;
        }
        return it->second.get();
    }

    node& parent_node(node& dir)
    {
        return dir.parent();
    }

    node* find_in_node(node& dir, const std::string_view name)
    {
        assert(dir.type() == ST_USERDIR || dir.type() == ST_ROOT);

        if (name.empty() || name == "." || name == ".." || name[0] == '\\')
            return nullptr;

        // Already known?
        for (auto child : dir.children_) {
            if (ci_equal(child->path().filename().string(), name))
                return child;
        }

        auto child_path = dir.path() / name;
        if (exists(child_path))
            return &make_node(&dir, child_path, is_directory(child_path) ? ST_USERDIR : ST_FILE);

        return nullptr;
    }

    uint32_t make_file_handle(node& n, std::fstream&& file, int32_t access)
    {
        assert((access == EXCLUSIVE_LOCK && n.access_count_ == -1) || (access == SHARED_LOCK && n.access_count_ > 0));
        assert(file.is_open() && file);
        uint32_t idx = 0;
        while (idx < file_handles_.size() && file_handles_[idx])
            ++idx;
        if (idx == file_handles_.size())
            file_handles_.push_back(nullptr);
        file_handles_[idx].reset(new file_handle { n, std::move(file), access });
        return idx;
    }

    file_handle* file_handle_from_idx(uint32_t idx)
    {
        if (idx >= file_handles_.size() || !file_handles_[idx]) {
#if FS_HANDLER_DEBUG > 0
            std::cout << "[HD] Invalid file handle " << idx << "\n";
#endif
            assert(!"Invalid file handle");
            return nullptr;
        }
        return file_handles_[idx].get();
    }

    bool close_file_handle(uint32_t idx)
    {
        if (idx >= file_handles_.size() || !file_handles_[idx])
            return false;
        file_handles_[idx].reset();
        return true;
    }

    node* find_next_node(node& dir, uint32_t last)
    {
        assert(dir.type() == ST_ROOT || dir.type() == ST_USERDIR);
        if (!last) {
            // First in iteration, add children (probably slow in larger folders)
            for (const auto& entry : fs::directory_iterator { dir.path() })
                (void)find_in_node(dir, entry.path().filename().string());
            return dir.children_.empty() ? nullptr : dir.children_.front();
        }
        auto last_node = node_from_key(last);
        if (!last_node) {
#if FS_HANDLER_DEBUG > 0
            std::cout << "[HD] Invalid directory key used\n";
#endif
            return nullptr;
        }
        auto it = std::find(dir.children_.begin(), dir.children_.end(), last_node);
        if (it == dir.children_.end()) {
#if FS_HANDLER_DEBUG > 0
            std::cout << "[HD] Invalid directory key used (not found in dir node)\n";
#endif
            return nullptr;
        }
        return ++it == dir.children_.end() ? nullptr : *it;
    }

    uint32_t delete_node(node& obj)
    {
        assert(obj.access_count_ == -1);
        auto it = nodes_.find(obj.id()); 
        if (it == nodes_.end() || it->second.get() != &obj)
            throw std::runtime_error { "[HD] Internal error: Tried to delete invalid node" };

        std::error_code ec;
        if (!remove(obj.path(), ec))
            return translate_error(ec);

        nodes_.erase(it);
        return NO_ERROR;
    }

    uint32_t rename(node& old_node, node& dir, const std::string& name)
    {
        assert(old_node.access_count_ == -1);

        auto it = nodes_.find(old_node.id());
        if (it == nodes_.end() || it->second.get() != &old_node)
            throw std::runtime_error { "[HD] Internal error: Tried to rename invalid node" };

        std::error_code ec;
        const auto new_path = dir.path() / name;
        fs::rename(old_node.path(), new_path, ec);
        if (ec)
            return translate_error(ec);

        const auto type = old_node.type();
        nodes_.erase(it);

        (void)make_node(&dir, new_path, type);

        return NO_ERROR;
    }

private:
    const fs::path base_dir_;
    std::map<uint32_t, std::unique_ptr<node>> nodes_;
    std::vector<std::unique_ptr<file_handle>> file_handles_;
    uint32_t next_id_ = 1;
    node& root_node_;
    uint32_t msg_port_ = 0;
    uint32_t dos_list_ = 0;

    node& make_node(node* parent, const fs::path& path, int32_t type)
    {
        auto [it, inserted] = nodes_.emplace(next_id_, std::unique_ptr<node>{ new node { next_id_, parent, path, type } });
        if (!inserted)
            throw std::runtime_error { "Internal error in filesystem_handler" };
        ++next_id_;
        return *it->second;
    }
};

} // unnamed namespace

class harddisk::impl final : public memory_area_handler, public autoconf_device {
public:
    explicit impl(memory_handler& mem, bool& cpu_active, const bool_func& should_disable_autoboot, const std::vector<std::string>& hdfilenames, const std::vector<std::string>& shared_folders)
        : autoconf_device { mem, *this, config }
        , mem_ { mem }
        , cpu_active_ { cpu_active }
        , should_disable_autoboot_ { should_disable_autoboot }
    {
        if (hdfilenames.empty() && shared_folders.empty())
            throw std::runtime_error { "Harddisk initialized with no filenames / shared folders" };
        if (hdfilenames.size() > 9)
            throw std::runtime_error { "Too many harddrive files" };

        std::vector<fs::path> sfs;
        for (const auto& sf : shared_folders) {
            auto p = absolute(fs::path(sf));
            if (!exists(p) || !is_directory(p))
                throw std::runtime_error { "Invalid path to shared folder: " + p.string() };
            sfs.push_back(p);
        }

        do_reset(hdfilenames, sfs);
    }

private:
    static constexpr board_config config {
        .type = ERTF_DIAGVALID,
        .size = 64 << 10,
        .product_number = 0x88,
        .hw_manufacturer = 1337,
        .serial_no = 1,
        .rom_vector_offset = EXPROM_BASE,
    };
    static constexpr uint32_t sector_size_bytes = 512;

    struct hd_info {
        std::string filename;
        std::fstream f;
        uint64_t size;
        uint32_t cylinders;
        uint8_t heads;
        uint16_t sectors_per_track;
    };

    struct partition_info {
        hd_info& hd;
        char name[32];
        uint32_t flags; // Flags for OpenDevice
        uint32_t block_size_bytes;
        uint32_t num_heads;
        uint32_t sectors_per_track;
        uint32_t reserved_blocks;
        uint32_t interleave;
        uint32_t lower_cylinder;
        uint32_t upper_cylinder; // Note: Inclusive
        uint32_t num_buffers;
        uint32_t mem_buffer_type;
        uint32_t max_transfer;
        uint32_t mask;
        uint32_t boot_priority;
        uint32_t dos_type;
        uint32_t boot_flags;
    };

    struct fs_info {
        uint32_t dos_type;
        uint32_t version;
        uint32_t patch_flags;
        std::vector<hunk> hunks;
        uint32_t seglist_bptr;
    };

    struct shared_folder_info {
        fs::path root_dir;
        std::string volume_name;
        std::unique_ptr<filesystem_handler> fs_handler;
    };

    memory_handler& mem_;
    bool& cpu_active_;
    bool_func should_disable_autoboot_;
    std::vector<std::unique_ptr<hd_info>> hds_;
    uint32_t ptr_hold_ = 0;    
    std::vector<uint8_t> buffer_;
    std::vector<partition_info> partitions_;
    std::vector<fs_info> filesystems_;
    std::vector<shared_folder_info> shared_folders_;
    uint32_t shared_folders_initialized_ = 0;

    static constexpr uint32_t local_ram_list_end = ~0U;
    static constexpr uint32_t local_ram_align = 8;
    static constexpr uint32_t local_ram_offset = (config.rom_vector_offset + sizeof(exprom) + 256 + 255) & -256; // reserve 256 bytes for "HW registers"
    static constexpr uint32_t local_ram_size = (config.size - local_ram_offset) & ~(local_ram_align - 1);
    uint8_t local_ram_[local_ram_size];
    uint32_t local_ram_heap_ptr_ = 0;

    void reset() override
    {
        // HACK to re-read RDB in case of format etc.
        std::vector<std::string> disk_filenames;
        std::vector<fs::path> shared_folders;
        for (const auto& hd : hds_)
            disk_filenames.push_back(hd->filename);
        for (const auto& sf : shared_folders_)
            shared_folders.push_back(sf.root_dir);
        do_reset(disk_filenames, shared_folders);
    }

    void do_reset(const std::vector<std::string>& disk_filenames, const std::vector<fs::path>& shared_folders)
    {
        ptr_hold_ = 0;
        shared_folders_initialized_ = 0;
        local_ram_init();
        hds_.clear();
        partitions_.clear();
        filesystems_.clear();
        init_disks(disk_filenames);
        shared_folders_.clear();
        for (const auto& p : shared_folders) {
            auto name = p.filename().string();
            if (name.length() > 30)
                name.erase(30);
            shared_folders_.push_back({ p, name, std::make_unique<filesystem_handler>(p) });
        }
    }

    void local_ram_init()
    {
        memset(local_ram_, 0, sizeof(local_ram_));
        local_ram_heap_ptr_ = 0;
        put_u32(&local_ram_[0], local_ram_list_end);
        put_u32(&local_ram_[4], local_ram_size);
    }

#ifdef LOCAL_RAM_DEBUG
    void local_ram_debug()
    {
        for (uint32_t p = local_ram_heap_ptr_;;) {
            if (p > local_ram_size - 8) {
                if (p != local_ram_list_end)
                    throw std::runtime_error { "[HD] Invalid local RAM free list end=$" + hexstring(p) };
                break;
            }

            const uint32_t anext = get_u32(&local_ram_[p]);
            const uint32_t asize = get_u32(&local_ram_[p + 4]);
            std::cout << "$" << hexfmt(p) << " size $" << hexfmt(asize) << " next $" << hexfmt(anext) << "\n";
            if (anext <= p || asize > local_ram_size || p + asize >= anext)
                throw std::runtime_error { "[HD] Invalid local RAM free list cur=$" + hexstring(p) + " next=$" + hexstring(anext) + " size=$" + hexstring(asize) };
            p = anext;
        }
    }
#endif

    uint32_t local_ram_alloc(uint32_t size)
    {
        size = (size + local_ram_align - 1) & ~(local_ram_align - 1);

        for (uint32_t p = local_ram_heap_ptr_; p < local_ram_size;) {
            const uint32_t anext = get_u32(&local_ram_[p]);
            const uint32_t asize = get_u32(&local_ram_[p + 4]);

            if (asize < size) {
                p = anext;
                continue;
            }

            const uint32_t rem_size = asize - size;
            if (rem_size) {
                put_u32(&local_ram_[p + size], anext);
                put_u32(&local_ram_[p + size + 4], rem_size);
                if (p == local_ram_heap_ptr_)
                    local_ram_heap_ptr_ = p + size;
            } else if (p == local_ram_heap_ptr_) {
                local_ram_heap_ptr_ = anext;
            }

            memset(&local_ram_[p], 0, size);
            return base_address() + local_ram_offset + p;
        }

#ifdef LOCAL_RAM_DEBUG
        std::cerr << "[HD] Local RAM alloc of " << size << " bytes failed\n";
#endif
        return 0;
    }

    void local_ram_free(uint32_t ptr, uint32_t size)
    {
        size = (size + local_ram_align - 1) & ~(local_ram_align - 1);
        ptr -= base_address() + local_ram_offset;
        if (ptr >= local_ram_size || size > local_ram_size || ptr + size > local_ram_size)
            throw std::runtime_error { "Bad pointer in harddisk::impl::local_ram_free" };

        uint32_t prev = local_ram_list_end;
        uint32_t next = local_ram_heap_ptr_;
        while (next < ptr && next < local_ram_size) {
            prev = next;
            next = get_u32(&local_ram_[next]);
        }

#ifdef LOCAL_RAM_DEBUG
        std::cout << std::string(40, '-') << "\n";
        std::cout << "Free $" << hexfmt(ptr) << " size $" << hexfmt(size) << " prev=$" << hexfmt(prev) << " next=$" << hexfmt(next) << "\n";
        std::cout << "Heap before:\n";
        local_ram_debug();
#endif

        if (prev == local_ram_list_end) {
            local_ram_heap_ptr_ = ptr;
        }  else if (auto prevsize = get_u32(&local_ram_[prev + 4]); prev + prevsize == ptr) {
            // join with previous
            ptr = prev;
            size += prevsize;
        }

        if (next < local_ram_size && ptr + size == next) {
            // join with next
            size += get_u32(&local_ram_[next + 4]);
            next = get_u32(&local_ram_[next]);
        }

        assert(ptr <= local_ram_size - 8);
        assert(ptr + size < next);

        put_u32(&local_ram_[ptr], next);
        put_u32(&local_ram_[ptr + 4], size);

#ifdef LOCAL_RAM_DEBUG
        std::cout << "Heap after:\n";
        local_ram_debug();
        std::cout << std::string(40, '-') << "\n";
#endif
    }

    void init_disks(const std::vector<std::string>& hdfilenames)
    {
        for (const auto& hdfilename : hdfilenames) {
            std::fstream hdfile { hdfilename, std::ios::binary | std::ios::in | std::ios::out };

            if (!hdfile.is_open())
                throw std::runtime_error { "Error opening " + hdfilename };
            hdfile.seekg(0, std::fstream::end);
            const uint64_t total_size = hdfile.tellg();

            if (total_size < 100 * 1024)
                throw std::runtime_error { "Invalid size for " + hdfilename + " " + std::to_string(total_size) };

            hds_.push_back(std::unique_ptr<hd_info>(new hd_info { hdfilename, std::move(hdfile), total_size, uint32_t(0), uint8_t(0), uint16_t(0) }));
            auto& hd = *hds_.back();

            const uint8_t* sector = disk_read(hd, 0, sector_size_bytes);
            if (check_structure(sector, IDNAME_RIGIDDISK)) {

                if (get_u32(&sector[16]) != 512) // Block size
                    throw std::runtime_error { hdfilename + " has unsupported/invalid blocksize" };

                const uint32_t num_cylinders = get_u32(&sector[64]);
                const uint32_t sectors_per_track = get_u32(&sector[68]);
                const uint32_t num_heads = get_u32(&sector[72]);

                std::cout << "C/H/S = " << num_cylinders << "/" << num_heads << "/" << sectors_per_track << "\n";

                hd.cylinders = num_cylinders;
                hd.heads = static_cast<uint8_t>(num_heads);
                hd.sectors_per_track = static_cast<uint16_t>(sectors_per_track);

                constexpr uint32_t end_of_list = 0xffffffff;
                uint32_t part_list = get_u32(&sector[28]);
                uint32_t fshdr_list = get_u32(&sector[32]);

                // Read partition list

                for (uint32_t cnt = 0; part_list != end_of_list; ++cnt) {
                    if (cnt >= 10)
                        throw std::runtime_error { hdfilename + " has too many partitions" };

                    sector = disk_read(hd, part_list * sector_size_bytes, sector_size_bytes);
                    if (!check_structure(sector, IDNAME_PARTITION))
                        throw std::runtime_error { hdfilename + " has an invalid partition list" };

                    partition_info pi { hd };
                    const uint8_t name_len = sector[36];
                    if (name_len >= sizeof(pi.name) - 1)
                        throw std::runtime_error { hdfilename + " has invalid partition name length" };
                    std::memcpy(pi.name, &sector[37], name_len);
                    pi.name[name_len] = 0;

                    if (!partition_name_ok(pi.name))
                        throw std::runtime_error { "Multiple partitions named " + std::string { pi.name } };

                    pi.flags = get_u32(&sector[32]);
                    pi.block_size_bytes = 4 * get_u32(&sector[132]);
                    pi.num_heads = get_u32(&sector[140]);
                    pi.sectors_per_track = get_u32(&sector[148]);
                    pi.reserved_blocks = get_u32(&sector[152]);
                    pi.interleave = get_u32(&sector[160]);
                    pi.lower_cylinder = get_u32(&sector[164]);
                    pi.upper_cylinder = get_u32(&sector[168]);
                    pi.num_buffers = get_u32(&sector[172]);
                    pi.mem_buffer_type = get_u32(&sector[176]);
                    pi.max_transfer = get_u32(&sector[180]);
                    pi.mask = get_u32(&sector[184]);
                    pi.boot_priority = get_u32(&sector[188]);
                    pi.dos_type = get_u32(&sector[192]);
                    pi.boot_flags = get_u32(&sector[20]); // bit0: bootable, bit1: no automount

                    part_list = get_u32(&sector[16]); // Next partition

                    if (pi.boot_flags & 2) {
                        std::cout << "[HD] Skipping partition \"" << pi.name << "\" DOS type: \"" << dos_type_string(pi.dos_type) << "\" - No auto mount\n";
                        continue;
                    }

                    partitions_.push_back(pi);

                    std::cout << "[HD] Found partition \"" << pi.name << "\" DOS type: \"" << dos_type_string(pi.dos_type) << "\" " << (pi.boot_flags & 1 ? "" : "not") << "bootable\n";
                }

                for (uint32_t cnt = 0; fshdr_list != end_of_list; ++cnt) {
                    if (cnt > 10)
                        throw std::runtime_error { hdfilename + " has an invalid file system header list (too many)" };

                    sector = disk_read(hd, fshdr_list * sector_size_bytes, sector_size_bytes);
                    if (!check_structure(sector, IDNAME_FILESYSHEADER))
                        throw std::runtime_error { hdfilename + " has an invalid file system header list" };

                    fshdr_list = get_u32(&sector[16]); // Next

                    const uint32_t dos_type = get_u32(&sector[32]);
                    const uint32_t version = get_u32(&sector[36]);
                    const uint32_t patch_flags = get_u32(&sector[40]);
                    uint32_t seg_list = get_u32(&sector[72]);

                    if (seg_list == end_of_list)
                        continue;

                    bool needed = true;
                    // Do we already have the same filesystem in the same or newer versoin?
                    for (const auto& fs : filesystems_) {
                        if (fs.dos_type == dos_type && fs.version >= version) {
                            needed = false;
                            break;
                        }
                    }
                    if (!needed)
                        continue;
                    needed = false;

                    // Check if any partitions use this filesystem
                    for (const auto& pi : partitions_) {
                        if (pi.dos_type == dos_type) {
                            needed = true;
                            break;
                        }
                    }
                    if (!needed)
                        continue;

                    if (patch_flags != 0x180) {
                        std::cerr << "[HD] Warning: Don't know how to handle patch flags $" << hexfmt(patch_flags) << "\n";
                    }

                    std::cout << "[HD] Found filesystem for : \"" << dos_type_string(dos_type) << "\" Version " << (version >> 16) << "." << (version & 0xffff) << " seg_list: " << (int)seg_list << "\n";

                    std::vector<uint8_t> fs_code;
                    while (seg_list != end_of_list) {
                        // No file system is ever going to be this large (right?)
                        if (fs_code.size() > 1024 * 1024)
                            throw std::runtime_error { hdfilename + " has an invalid segment list" };

                        sector = disk_read(hd, seg_list * sector_size_bytes, sector_size_bytes);
                        const auto size_bytes = get_u32(&sector[4]) * 4;
                        if (size_bytes < 24 || size_bytes > sector_size_bytes)
                            throw std::runtime_error { hdfilename + " has an invalid segment list" };
                        if (!check_structure(sector, IDNAME_LOADSEG, size_bytes))
                            throw std::runtime_error { hdfilename + " has an invalid segment list" };

                        fs_code.insert(fs_code.end(), &sector[20], &sector[size_bytes]);

                        seg_list = get_u32(&sector[16]); // Next
                    }

                    filesystems_.push_back(fs_info {
                        dos_type,
                        version,
                        patch_flags,
                        parse_hunk_file(fs_code),
                        0 });
                }

                continue;
            }

            // HDF: Mount as a single partition

            const uint32_t num_heads = 1;
            const uint32_t sectors_per_track = 32;

            const auto cyl_size = num_heads * sectors_per_track * sector_size_bytes;
            if (total_size > 504 * 1024 * 1024) // Limit to 504MB for now (probably need more heads)
                throw std::runtime_error { "Invalid size for " + hdfilename + " " + std::to_string(total_size) };
            const uint32_t num_cylinders = static_cast<uint32_t>(total_size / cyl_size);

            hd.cylinders = num_cylinders;
            hd.heads = static_cast<uint8_t>(num_heads);
            hd.sectors_per_track = static_cast<uint16_t>(sectors_per_track);

            partitions_.push_back(partition_info {
                hd,
                "",
                0,
                sector_size_bytes,
                num_heads,
                sectors_per_track,
                2,
                0,
                0,
                num_cylinders - 1,
                1, // one buffer
                0,
                0x7ffe, // Max transfer
                0xfffffffe, // Mask
                0, // Boot priority
                0x444f5300, // 'DOS\0'
                0 << 24 | 1, // Bootable
            });
            for (uint32_t num = 0;; ++num) {
                std::string name = "DH" + std::to_string(num);
                if (partition_name_ok(name)) {
                    strcpy(partitions_.back().name, name.c_str());
                    break;
                }
            }
            std::cout << "[HD] Plain HDF \"" << hdfilename << "\" will become " << partitions_.back().name << " C/H/S: " << hd.cylinders << "/" << static_cast<int>(hd.heads) << "/" << static_cast<int>(hd.sectors_per_track) << "\n";
        }
    }

    void handle_state(state_file& sf) override
    {
        const state_file::scope scope { sf, "Harddisk", 1 };
        sf.handle(ptr_hold_);
    }

    bool partition_name_ok(const std::string& name)
    {
        return std::find_if(partitions_.begin(), partitions_.end(), [&name](const partition_info& pi) { return pi.name == name; }) == partitions_.end();
    }

    const uint8_t* disk_read(hd_info& hd, uint64_t offset, uint32_t len)
    {
        assert(len && len % sector_size_bytes == 0 && offset % sector_size_bytes == 0 && offset < hd.size && len < hd.size && offset + len <= hd.size);
        buffer_.resize(len);
        hd.f.seekg(offset);
        hd.f.read(reinterpret_cast<char*>(&buffer_[0]), len);
        if (!hd.f)
            throw std::runtime_error { "Error reading from " + hd.filename };
        return &buffer_[0];
    }

    uint8_t read_u8(uint32_t, uint32_t offset) override
    {
        if (offset >= config.rom_vector_offset && offset < config.rom_vector_offset + sizeof(exprom)) {
            return exprom[offset - config.rom_vector_offset];
        } else if (offset >= local_ram_offset) {
            return local_ram_[offset - local_ram_offset];
        }

        std::cerr << "harddisk: Read U8 offset $" << hexfmt(offset) << "\n";
        return 0;
    }

    uint16_t read_u16(uint32_t, uint32_t offset) override
    {
        if (offset >= config.rom_vector_offset && offset + 1 < config.rom_vector_offset + sizeof(exprom)) {
            offset -= config.rom_vector_offset;
            return static_cast<uint16_t>(exprom[offset] << 8 | exprom[offset + 1]);
        }
        const uint32_t special_offset = static_cast<uint32_t>(config.rom_vector_offset + sizeof(exprom));

        if (offset == special_offset) {
            return static_cast<uint16_t>(partitions_.size());
        } else if (offset == special_offset + 2) {
            return static_cast<uint16_t>(filesystems_.size());
        } else if (offset == special_offset + 4) {
            return should_disable_autoboot_();
        } else if (offset == special_offset + 6) {
            return static_cast<uint16_t>(shared_folders_.size());
        } else if (offset >= local_ram_offset) {
            return get_u16(&local_ram_[offset - local_ram_offset]);
        }

        std::cerr << "harddisk: Read U16 offset $" << hexfmt(offset) << "\n";
        return 0;
    }

    void write_u8(uint32_t, uint32_t offset, uint8_t val) override
    {
        if (offset >= local_ram_offset) {
            local_ram_[offset - local_ram_offset] = val;
        } else {
            std::cerr << "harddisk: Invalid write to offset $" << hexfmt(offset) << " val $" << hexfmt(val) << "\n";
        }
    }

    void write_u16(uint32_t, uint32_t offset, uint16_t val) override
    {
        const uint32_t special_offset = static_cast<uint32_t>(config.rom_vector_offset + sizeof(exprom));
        if (offset == special_offset) {
            ptr_hold_ = val << 16 | (ptr_hold_ & 0xffff);
            return;
        } else if (offset == special_offset + 2) {
            ptr_hold_ = (ptr_hold_ & 0xffff0000) | val;
        } else if (offset == special_offset + 4) {
            if (!ptr_hold_ || (ptr_hold_ & 1) || !cpu_active_) {
                std::cerr << "harddisk: Invalid conditions! written val = $" << hexfmt(val) << " ptr_hold = $" << hexfmt(ptr_hold_) << " cpu_active_ = " << cpu_active_ << "\n";
                return;
            }
            cpu_active_ = false;

            try {
                if (val == 0xfede)
                    handle_disk_cmd();
                else if (val == 0xfedf)
                    handle_init();
                else if (val == 0xfee0)
                    handle_fs_resource();
                else if (val == 0xfee1)
                    handle_fs_info_req();
                else if (val == 0xfee2)
                    handle_fs_initseg();
                else if (val == 0xfee3)
                    handle_volume_get_id();
                else if (val == 0xfee4)
                    handle_volume_init();
                else if (val == 0xfee5)
                    handle_volume_packet();
                else
                    throw std::runtime_error { "Invalid HD command: $" + hexstring(val) };
            } catch (...) {
                cpu_active_ = true;
                ptr_hold_ = 0;
                throw;
            }

            cpu_active_ = true;
            ptr_hold_ = 0;
        } else if (offset >= local_ram_offset) {
            put_u16(&local_ram_[offset - local_ram_offset], val);
        } else {
            std::cerr << "harddisk: Invalid write to offset $" << hexfmt(offset) << " val $" << hexfmt(val) << "\n";
        }
    }

    void handle_init()
    {
        // keep in check with exprom.asm
        constexpr uint16_t devn_dosName = 0x00; // APTR  Pointer to DOS file handler name
        constexpr uint16_t devn_unit = 0x08; // ULONG Unit number
        constexpr uint16_t devn_flags = 0x0C; // ULONG OpenDevice flags
        constexpr uint16_t devn_sizeBlock = 0x14; // ULONG # longwords in a block
        constexpr uint16_t devn_secOrg = 0x18; // ULONG sector origin -- unused
        constexpr uint16_t devn_numHeads = 0x1C; // ULONG number of surfaces
        constexpr uint16_t devn_secsPerBlk = 0x20; // ULONG secs per logical block
        constexpr uint16_t devn_blkTrack = 0x24; // ULONG secs per track
        constexpr uint16_t devn_resBlks = 0x28; // ULONG reserved blocks -- MUST be at least 1!
        constexpr uint16_t devn_interleave = 0x30; // ULONG interleave
        constexpr uint16_t devn_lowCyl = 0x34; // ULONG lower cylinder
        constexpr uint16_t devn_upperCyl = 0x38; // ULONG upper cylinder
        constexpr uint16_t devn_numBuffers = 0x3C; // ULONG number of buffers
        constexpr uint16_t devn_memBufType = 0x40; // ULONG Type of memory for AmigaDOS buffers
        constexpr uint16_t devn_transferSize = 0x44; // LONG  largest transfer size (largest signed #)
        constexpr uint16_t devn_addMask = 0x48; // ULONG address mask
        constexpr uint16_t devn_bootPrio = 0x4c; // ULONG boot priority
        constexpr uint16_t devn_dName = 0x50; // char[4] DOS file handler name
        constexpr uint16_t devn_bootflags = 0x54; // boot flags (not part of DOS packet)
        constexpr uint16_t devn_segList = 0x58; // filesystem segment list (not part of DOS packet)

        // unit, dosName, execName and tableSize is filled by expansion ROM

        const uint32_t unit = mem_.read_u32(ptr_hold_ + devn_unit);

        if (unit >= partitions_.size())
            throw std::runtime_error { "Invalid HD partition used for initialization" };

        const auto& part = partitions_[unit];

        std::cout << "[HD] Initializing partition " << unit << " " << dos_type_string(part.dos_type) << " \"" << part.name << "\"\n";
        const uint32_t name_ptr = mem_.read_u32(ptr_hold_ + devn_dosName);
        for (uint32_t i = 0; i < sizeof(part.name); ++i)
            mem_.write_u8(name_ptr + i, part.name[i]);

        mem_.write_u32(ptr_hold_ + devn_flags, part.flags);
        mem_.write_u32(ptr_hold_ + devn_sizeBlock, part.block_size_bytes / 4);
        mem_.write_u32(ptr_hold_ + devn_secOrg, 0);
        mem_.write_u32(ptr_hold_ + devn_numHeads, part.num_heads);
        mem_.write_u32(ptr_hold_ + devn_secsPerBlk, 1);
        mem_.write_u32(ptr_hold_ + devn_blkTrack, part.sectors_per_track);
        mem_.write_u32(ptr_hold_ + devn_interleave, part.interleave);
        mem_.write_u32(ptr_hold_ + devn_resBlks, part.reserved_blocks);
        mem_.write_u32(ptr_hold_ + devn_lowCyl, part.lower_cylinder);
        mem_.write_u32(ptr_hold_ + devn_upperCyl, part.upper_cylinder);
        mem_.write_u32(ptr_hold_ + devn_numBuffers, part.num_buffers);
        mem_.write_u32(ptr_hold_ + devn_memBufType, part.mem_buffer_type);
        mem_.write_u32(ptr_hold_ + devn_transferSize, part.max_transfer);
        mem_.write_u32(ptr_hold_ + devn_addMask, part.mask);
        mem_.write_u32(ptr_hold_ + devn_bootPrio, part.boot_priority);
        mem_.write_u32(ptr_hold_ + devn_dName, part.dos_type);
        mem_.write_u32(ptr_hold_ + devn_bootflags, part.boot_flags);

        uint32_t seglist_bptr = 0;
        for (const auto& fs : filesystems_) {
            if (part.dos_type == fs.dos_type) {
                std::cout << "[HD] Partition " << unit << " seglist: $" << hexfmt(fs.seglist_bptr * 4) << "\n";
                seglist_bptr = fs.seglist_bptr;
                break;
            }
        }

        mem_.write_u32(ptr_hold_ + devn_segList, seglist_bptr);
    }

    void do_scsi_cmd(hd_info& hd, scsi_cmd& sc)
    {
        std::vector<uint8_t> cmd(sc.scsi_CmdLength);
        for (uint32_t i = 0; i < sc.scsi_CmdLength; ++i)
            cmd[i] = mem_.read_u8(sc.scsi_Command + i);

        static constexpr bool scsi_debug = false;

        if constexpr (scsi_debug) {
            std::cout << "[HD] SCSI command for " << hd.filename << ": ";
            hexdump(std::cout, cmd.data(), cmd.size());
        }

        if (sc.scsi_CmdLength < 6) {
            std::cerr << "[HD] Invalid SCSI command length: " << sc.scsi_CmdLength << "\n";
            sc.scsi_Status = 2; // check condition (TODO: sense data)
            return;
        }

        auto copy_data = [&](const uint8_t* data, size_t len) {
            const uint32_t actlen = std::min(sc.scsi_Length, static_cast<uint32_t>(len));
            for (uint32_t i = 0; i < actlen; ++i)
                mem_.write_u8(sc.scsi_Data + i, data[i]);

            if constexpr (scsi_debug) {
                std::cout << "[HD] Returning: ";
                hexdump(std::cout, data, actlen);
            }

            sc.scsi_Actual = actlen;
            sc.scsi_CmdActual = sc.scsi_CmdLength;
            sc.scsi_Status = 0;
            sc.scsi_SenseActual = 0;
        };

        if (cmd[0] == 0x25 && sc.scsi_CmdLength == 10) {
            // READ CAPACITY (10)
            uint8_t data[8] = {
                0,
            };
            const uint32_t max_block = static_cast<uint32_t>(std::min(uint64_t(0xffffffff), hd.size / sector_size_bytes - 1));
            if (cmd[8] & 1) { // pmi
                uint32_t lba = get_u32(&cmd[2]);
                lba += hd.sectors_per_track * hd.heads;
                lba /= hd.sectors_per_track * hd.heads;
                lba *= hd.sectors_per_track * hd.heads;
                put_u32(&data[0], std::min(max_block, lba));
            } else {
                put_u32(&data[0], max_block);
            }
            put_u32(&data[4], sector_size_bytes);
            copy_data(data, sizeof(data));
            return;
        }

        if (cmd[0] == 0x12 && sc.scsi_CmdLength == 6) {
            // INQUIRY
            uint8_t data[36] = {
                0,
            };

            auto copy_string = [](uint8_t* d, const char* s, int len) {
                while (*s && len--)
                    *d++ = *s++;
                while (len--)
                    *d++ = ' ';
            };

            data[2] = 2; // Version
            data[4] = static_cast<uint8_t>(sizeof(data) - 4); // Additional length
            copy_string(&data[8], "AmiEmu", 8); // vendor
            copy_string(&data[16], "Virtual HD", 16); // product id
            copy_string(&data[32], "0.1", 4); // revision

            copy_data(data, sizeof(data));
            return;
        }

        if (cmd[0] == 0x1a && sc.scsi_CmdLength == 6 && (cmd[2] == 3 || cmd[2] == 4)) {
            // MODE SENSE (6) with PC = 0 and Page Code = 3 (Format Parameters page) or 4 (Rigid drive geometry parameters)
            uint8_t data[256];
            memset(data, 0, sizeof(data));
            data[3] = 8;
            put_u32(&data[4], static_cast<uint32_t>(std::min(uint64_t(0xffffffff), hd.size / sector_size_bytes)));
            put_u32(&data[8], sector_size_bytes);

            uint8_t* page = &data[12];

            page[0] = cmd[2]; // page code
            page[1] = 0x16; // page length

            if (cmd[2] == 3) {
                put_u16(&page[2], 1); // tracks per zone
                put_u16(&page[10], hd.sectors_per_track);
                put_u16(&page[12], sector_size_bytes); // data bytes per physical sector
                put_u16(&page[14], 1); // interleave
                page[20] = 0x80; // Drive type
            } else {
                assert(cmd[2] == 4);
                page[14] = page[2] = static_cast<uint8_t>((hd.cylinders >> 16) & 0xff);
                page[15] = page[3] = static_cast<uint8_t>((hd.cylinders >> 8) & 0xff);
                page[16] = page[4] = static_cast<uint8_t>((hd.cylinders >> 0) & 0xff);
                page[5] = hd.heads;
                put_u16(&page[20], 5400); // rotation speed
            }
            page += page[1] + 2;

            data[0] = static_cast<uint8_t>(page - data - 1);

            copy_data(data, page - data);
            return;
        }

        if (cmd[0] == 0x37 && sc.scsi_CmdLength == 10) { // READ DEFECT DATA
            uint8_t data[4] = { 0, static_cast<uint8_t>(cmd[1] & 0x1f), 0, 0 };
            copy_data(data, sizeof(data));
            return;
        }

        std::cerr << "[HD] Unsupported SCSI command: ";
        hexdump(std::cerr, cmd.data(), cmd.size());
        //#define SCSI_INVALID_COMMAND 0x20

        sc.scsi_Status = /*SCSI_INVALID_COMMAND*/ 0x20; // SCSI status
        sc.scsi_Actual = 0; // sc.scsi_Length;
        sc.scsi_CmdActual = 0; // sc.scsi_CmdLength; // Whole command used
        sc.scsi_SenseActual = 0; // Not used
    }

    void handle_disk_cmd()
    {
        // Standard commands
        // constexpr uint16_t CMD_INVALID     = 0;
        constexpr uint16_t CMD_RESET = 1;
        constexpr uint16_t CMD_READ = 2;
        constexpr uint16_t CMD_WRITE = 3;
        constexpr uint16_t CMD_UPDATE = 4;
        constexpr uint16_t CMD_CLEAR = 5;
        constexpr uint16_t CMD_STOP = 6;
        constexpr uint16_t CMD_START = 7;
        constexpr uint16_t CMD_FLUSH = 8;
        constexpr uint16_t CMD_NONSTD = 9;
        constexpr uint16_t TD_MOTOR = CMD_NONSTD + 0; // 09
        constexpr uint16_t TD_SEEK = CMD_NONSTD + 1; // 0A
        constexpr uint16_t TD_FORMAT = CMD_NONSTD + 2; // 0B
        constexpr uint16_t TD_REMOVE = CMD_NONSTD + 3; // 0C
        constexpr uint16_t TD_CHANGENUM = CMD_NONSTD + 4; // 0D
        constexpr uint16_t TD_CHANGESTATE = CMD_NONSTD + 5; // 0E
        constexpr uint16_t TD_PROTSTATUS = CMD_NONSTD + 6; // 0F
        // constexpr uint16_t TD_RAWREAD      = CMD_NONSTD + 7;  // 10
        // constexpr uint16_t TD_RAWWRITE     = CMD_NONSTD + 8;  // 11
        // constexpr uint16_t TD_GETDRIVETYPE = CMD_NONSTD + 9;  // 12
        // constexpr uint16_t TD_GETNUMTRACKS = CMD_NONSTD + 10; // 13
        constexpr uint16_t TD_ADDCHANGEINT = CMD_NONSTD + 11; // 14
        constexpr uint16_t TD_REMCHANGEINT = CMD_NONSTD + 12; // 15
        constexpr uint16_t HD_SCSICMD = 28;

        constexpr uint32_t IO_UNIT = 0x18;
        constexpr uint32_t IO_COMMAND = 0x1C;
        constexpr uint32_t IO_ERROR = 0x1F;
        constexpr uint32_t IO_ACTUAL = 0x20;
        constexpr uint32_t IO_LENGTH = 0x24;
        constexpr uint32_t IO_DATA = 0x28;
        constexpr uint32_t IO_OFFSET = 0x2C;

        constexpr uint16_t devunit_UnitNum = 0x2A;

        const auto unit = mem_.read_u32(mem_.read_u32(ptr_hold_ + IO_UNIT) + devunit_UnitNum); // grab from private field
        const auto cmd = mem_.read_u16(ptr_hold_ + IO_COMMAND);
        const auto len = mem_.read_u32(ptr_hold_ + IO_LENGTH);
        const auto data = mem_.read_u32(ptr_hold_ + IO_DATA);
        const auto ofs = mem_.read_u32(ptr_hold_ + IO_OFFSET);

        if (unit >= partitions_.size()) {
            throw std::runtime_error { "Invalid partition (unit) $" + hexstring(unit) + " in IORequest" };
        }
        auto& hd = partitions_[unit].hd;

        // std::cerr << "[HD]: Command=$" << hexfmt(cmd) << " Unit=" << unit << " Length=$" << hexfmt(len) << " Data=$" << hexfmt(data) << " Offset=$" << hexfmt(ofs) << "\n";
        switch (cmd) {
        case CMD_READ:
        case CMD_WRITE:
        case TD_FORMAT:
            // TODO: Maybe check against partition offset/size?
            if (ofs > hd.size || len > hd.size || ofs > hd.size - len || ofs % sector_size_bytes) {
                std::cerr << "[HD] Invalid offset $" << hexfmt(ofs) << " length=$" << hexfmt(len) << "\n";
                mem_.write_u8(ptr_hold_ + IO_ERROR, static_cast<uint8_t>(IOERR_BADADDRESS));
            } else if (len % sector_size_bytes) {
                std::cerr << "[HD] Invalid length $" << hexfmt(len) << "\n";
                mem_.write_u8(ptr_hold_ + IO_ERROR, static_cast<uint8_t>(IOERR_BADADDRESS));
            } else {
                if (cmd == CMD_READ) {
                    const uint8_t* diskdata = disk_read(hd, ofs, len);
                    for (uint32_t i = 0; i < len; i += 4)
                        mem_.write_u32(data + i, get_u32(&diskdata[i]));
                } else {
                    buffer_.resize(len);
                    for (uint32_t i = 0; i < len; i += 4)
                        put_u32(&buffer_[i], mem_.read_u32(data + i));
                    hd.f.seekp(ofs);
                    hd.f.write(reinterpret_cast<const char*>(&buffer_[0]), len);
                    if (!hd.f)
                        throw std::runtime_error { "Error writing to " + hd.filename };
                }
                mem_.write_u32(ptr_hold_ + IO_ACTUAL, len);
                mem_.write_u8(ptr_hold_ + IO_ERROR, 0);
            }
            break;
        case CMD_RESET:
        case CMD_UPDATE:
        case CMD_CLEAR:
        case CMD_STOP:
        case CMD_START:
        case CMD_FLUSH:
        case TD_MOTOR:
        case TD_SEEK:
        case TD_REMOVE:
        case TD_CHANGENUM:
        case TD_CHANGESTATE:
        case TD_PROTSTATUS:
        case TD_ADDCHANGEINT:
        case TD_REMCHANGEINT:
            mem_.write_u32(ptr_hold_ + IO_ACTUAL, 0);
            mem_.write_u8(ptr_hold_ + IO_ERROR, 0);
            break;
        case HD_SCSICMD: {
            constexpr uint16_t scsi_Data = 0x00;
            constexpr uint16_t scsi_Length = 0x04;
            constexpr uint16_t scsi_Actual = 0x08;
            constexpr uint16_t scsi_Command = 0x0c;
            constexpr uint16_t scsi_CmdLength = 0x10;
            constexpr uint16_t scsi_CmdActual = 0x12;
            constexpr uint16_t scsi_Flags = 0x14;
            constexpr uint16_t scsi_Status = 0x15;
            constexpr uint16_t scsi_SenseData = 0x16;
            constexpr uint16_t scsi_SenseLength = 0x1a;
            constexpr uint16_t scsi_SenseActual = 0x1c;
            constexpr uint16_t scsi_SizeOf = 0x1e;
            if (ofs || (len != scsi_SizeOf && len != 0x22)) { // HdToolBox seems to set length to $22?
                std::cerr << "[HD] Bad HD_SCSICMD offset $" << hexfmt(ofs) << " length=$" << hexfmt(len) << "\n";
                mem_.write_u8(ptr_hold_ + IO_ERROR, static_cast<uint8_t>(IOERR_BADADDRESS));
                break;
            }

            scsi_cmd scmd {};
            scmd.scsi_Data = mem_.read_u32(data + scsi_Data);
            scmd.scsi_Length = mem_.read_u32(data + scsi_Length);
            scmd.scsi_Command = mem_.read_u32(data + scsi_Command);
            scmd.scsi_CmdLength = mem_.read_u16(data + scsi_CmdLength);
            scmd.scsi_Flags = mem_.read_u8(data + scsi_Flags);
            scmd.scsi_SenseData = mem_.read_u32(data + scsi_SenseData);
            scmd.scsi_SenseLength = mem_.read_u16(data + scsi_SenseLength);

            do_scsi_cmd(hd, scmd);
            mem_.write_u32(data + scsi_Actual, scmd.scsi_Actual);
            mem_.write_u32(data + scsi_CmdActual, scmd.scsi_CmdActual);
            mem_.write_u8(data + scsi_Status, scmd.scsi_Status);
            mem_.write_u16(data + scsi_SenseActual, scmd.scsi_SenseActual);
            mem_.write_u8(ptr_hold_ + IO_ERROR, 0);
            mem_.write_u32(ptr_hold_ + IO_ACTUAL, /*len*/ scmd.scsi_Actual);
            break;
        }
        default:
            std::cerr << "[HD] Unsupported command $" << hexfmt(cmd) << "\n";
            mem_.write_u8(ptr_hold_ + IO_ERROR, static_cast<uint8_t>(IOERR_NOCMD));
        }
    }

    void handle_fs_resource()
    {
        if (!ptr_hold_)
            return;

        // Grab head of fsr_FileSysEntries
        uint32_t node = mem_.read_u32(ptr_hold_ + 0x12);
        for (;;) {
            const auto succ = mem_.read_u32(node);
            if (!succ)
                break;

            const uint32_t dos_type = mem_.read_u32(node + 0x0e); // fsr_DosType
            const uint32_t version = mem_.read_u32(node + 0x12); // fsr_Version

            // Check if we were going to provide this FS
            for (auto it = filesystems_.begin(); it != filesystems_.end();) {
                if (it->dos_type == dos_type && it->version <= version) {
                    it = filesystems_.erase(it);
                } else {
                    ++it;
                }
            }
            node = succ;
        }
    }

    void handle_fs_info_req()
    {
        // Keep structure in sync with exprom.asm
        static constexpr uint16_t fsinfo_num = 0x00;
        static constexpr uint16_t fsinfo_dosType = 0x02;
        static constexpr uint16_t fsinfo_version = 0x06;
        static constexpr uint16_t fsinfo_numHunks = 0x0a;
        static constexpr uint16_t fsinfo_hunk = 0x0e;

        const uint16_t fs_num = mem_.read_u16(ptr_hold_ + fsinfo_num);
        if (fs_num >= filesystems_.size())
            throw std::runtime_error { "Request for invalid filesystem number " + std::to_string(fs_num) };
        const auto& fs = filesystems_[fs_num];
        mem_.write_u32(ptr_hold_ + fsinfo_dosType, fs.dos_type);
        mem_.write_u32(ptr_hold_ + fsinfo_version, fs.version);
        mem_.write_u32(ptr_hold_ + fsinfo_numHunks, static_cast<uint16_t>(fs.hunks.size()));
        for (uint32_t i = 0; i < fs.hunks.size(); ++i)
            mem_.write_u32(ptr_hold_ + fsinfo_hunk + i * 4, fs.hunks[i].flags);
    }

    void handle_fs_initseg()
    {
        static constexpr uint16_t fsinitseg_hunk = 0;
        static constexpr uint16_t fsinitseg_num = 12;
        const uint32_t fs_num = mem_.read_u32(ptr_hold_ + fsinitseg_num);
        if (fs_num >= filesystems_.size())
            throw std::runtime_error { "Request for invalid filesystem number " + std::to_string(fs_num) };

        uint32_t segptr[max_hunks] = {
            0,
        };
        auto& fs = filesystems_[fs_num];
        assert(fs.seglist_bptr == 0);
        for (uint32_t i = 0; i < fs.hunks.size(); ++i) {
            segptr[i] = mem_.read_u32(ptr_hold_ + fsinitseg_hunk + i * 4);
            if (!segptr[i])
                throw std::runtime_error { "Memory allocation failed inside AmigaOS" };
        }
        for (uint32_t i = 0; i < fs.hunks.size(); ++i) {
            mem_.write_u32(segptr[i], static_cast<uint32_t>(fs.hunks[i].data.size()) - 8); // Hunk size
            mem_.write_u32(segptr[i] + 4, i == fs.hunks.size() - 1 ? 0 : (segptr[i + 1] + 4) >> 2); // Link hunks
            const uint32_t start = segptr[i] + 8;
            for (uint32_t j = 0; j < fs.hunks[i].data.size(); j += 4)
                mem_.write_u32(start + j, get_u32(&fs.hunks[i].data[j]));

            for (const auto& hr : fs.hunks[i].relocs) {
                const uint32_t dst_start = segptr[hr.dst_hunk] + 8;
                for (const auto ofs : hr.offsets) {
                    mem_.write_u32(start + ofs, mem_.read_u32(start + ofs) + dst_start);
                }
            }
        }

        std::cout << "[HD] Filesystem " << dos_type_string(fs.dos_type) << " loaded. SegList at $" << hexfmt(segptr[0] + 4) << "\n";
        fs.seglist_bptr = (segptr[0] + 4) >> 2;
    }

    void handle_volume_get_id()
    {
        if (shared_folders_initialized_ >= shared_folders_.size())
            throw std::runtime_error { "Too many shared folders being initialized (get id)" };
        auto name = shared_folders_[shared_folders_initialized_].volume_name;
        const auto l = static_cast<uint8_t>(name.length());
        const auto addr = local_ram_alloc(l+2);
        if (!addr)
            throw std::runtime_error { "Could not allocate local HD memory for shared folder name" };

        // Name as NUL-terminated BSTR
        mem_.write_u8(addr, l);
        for (int i = 0; i < l; ++i)
            mem_.write_u8(addr + 1 + i, name[i]);
        mem_.write_u8(addr + 1 + l, 0);

        std::cout << "[HD] Volume getting id ptr=$" << hexfmt(ptr_hold_) << " " << name << "\n";
        mem_.write_u32(ptr_hold_ + handler_Id, shared_folders_initialized_);
        mem_.write_u32(ptr_hold_ + handler_DevName, BPTR(addr));
        ++shared_folders_initialized_;
    }

    void handle_volume_init()
    {
        const auto id = mem_.read_u32(ptr_hold_ + handler_Id);
        if (id >= shared_folders_initialized_)
            throw std::runtime_error { "Too many shared folders being initialized (init)" };
        std::cout << "[HD] Volume initialized ptr=$" << hexfmt(ptr_hold_) << " id=" << id << " " << shared_folders_[id].volume_name << "\n";

        const auto msg_port = mem_.read_u32(ptr_hold_ + handler_MsgPort);
        const auto dos_list = mem_.read_u32(ptr_hold_ + handler_DosList);
        shared_folders_[id].fs_handler->init(msg_port, dos_list);
    }

    void handle_volume_packet();

    // 0 => root dir
    filesystem_handler::node* node_from_lock(filesystem_handler& fs_handler, uint32_t bptr_to_lock);    
    // Perform an operation on "name" releative to "lock" (operation = 0 => lock, ST_USERDIR/ST_FILE => create, node* => rename the node to the pointed to object)
    std::variant<uint32_t, filesystem_handler::node*> node_operation(filesystem_handler& fs_handler, uint32_t bptr_to_lock, uint32_t bptr_to_name, int32_t access, std::variant<int32_t, filesystem_handler::node*> operation = 0);
    void fill_file_info(filesystem_handler::node& node, uint32_t fib_cptr);
    void fill_info_data(filesystem_handler& fs_handler, uint32_t id_cptr);

    // Alloc FileLock for already locked node (returns BPTR to allocated memory, or 0 on error)
    uint32_t make_lock(filesystem_handler& fs_handler, filesystem_handler::node& node, int32_t access);

    void action_locate_object(filesystem_handler& fs_handler, DosPacket& dp);
    void action_free_lock(filesystem_handler& fs_handler, DosPacket& dp);
    void action_delete_object(filesystem_handler& fs_handler, DosPacket& dp);
    void action_rename_object(filesystem_handler& fs_handler, DosPacket& dp);
    void action_copy_dir(filesystem_handler& fs_handler, DosPacket& dp);
    void action_set_protect(filesystem_handler& fs_handler, DosPacket& dp);
    void action_create_dir(filesystem_handler& fs_handler, DosPacket& dp);
    void action_examine_object(filesystem_handler& fs_handler, DosPacket& dp);
    void action_examine_next(filesystem_handler& fs_handler, DosPacket& dp);
    void action_disk_info(filesystem_handler& fs_handler, DosPacket& dp);
    void action_info(filesystem_handler& fs_handler, DosPacket& dp);
    void action_parent(filesystem_handler& fs_handler, DosPacket& dp);
    void action_same_lock(filesystem_handler& fs_handler, DosPacket& dp);
    void action_find(filesystem_handler& fs_handler, DosPacket& dp);
    void action_end(filesystem_handler& fs_handler, DosPacket& dp);
    void action_seek(filesystem_handler& fs_handler, DosPacket& dp);
    void action_read(filesystem_handler& fs_handler, DosPacket& dp);
    void action_write(filesystem_handler& fs_handler, DosPacket& dp);
};

filesystem_handler::node* harddisk::impl::node_from_lock(filesystem_handler& fs_handler, uint32_t bptr_to_lock)
{
    if (!bptr_to_lock)
        return &fs_handler.root_node();
    return fs_handler.node_from_key(mem_.read_u32(BADDR(bptr_to_lock) + fl_Key));
}

std::variant<uint32_t, filesystem_handler::node*> harddisk::impl::node_operation(filesystem_handler& fs_handler, uint32_t bptr_to_lock, uint32_t bptr_to_name, int32_t access, std::variant<int32_t, filesystem_handler::node*> operation)
{
    auto* node = node_from_lock(fs_handler, bptr_to_lock);

    if (!node)
        return ERROR_INVALID_LOCK;

    auto name = read_bcpl_string(mem_, BADDR(bptr_to_name));
    auto i = name.find_first_of(':'), l = name.length();
    if (i != std::string::npos) {
        ++i;
    } else {
        i = 0;
    }

    while (i < l && name[i] == '/') {
        if (node == &fs_handler.root_node())
            return ERROR_OBJECT_NOT_FOUND;
        node = &node->parent();
        ++i;
    }

    while (node && i < l) {
        if (node->type() != ST_ROOT && node->type() != ST_USERDIR) {
            return ERROR_OBJECT_WRONG_TYPE;
        }
        auto comp_end = name.find_first_of('/', i);
        if (comp_end == std::string::npos) {
            auto& parent = *node;
            auto filename = name.substr(i);
            node = fs_handler.find_in_node(parent, filename);

            if (operation.index() == 1) {
                // Rename
                if (node)
                    return ERROR_OBJECT_EXISTS;
                return fs_handler.rename(*std::get<1>(operation), parent, filename);
            }

            const auto create_type = std::get<0>(operation);

            if (create_type == ST_FILE) {
                // TODO: This could be better...
                // NOTE: could be overwriting file here!!
                {
                    // HACK: Create/re-rwrite file here
                    std::fstream temp_file { parent.path() / filename, std::ios::out | std::ios::binary };
                    if (!temp_file || !temp_file.is_open())
                        return ERROR_WRITE_PROTECTED; // FIXME
                }
                node = fs_handler.find_in_node(parent, filename);
                assert(node);
            } else if (create_type == ST_USERDIR) {
                if (node)
                    return ERROR_OBJECT_EXISTS;
                create_directory(parent.path() / filename);
                node = fs_handler.find_in_node(parent, filename);
                assert(node);
            } else if (create_type) {
                throw std::runtime_error { "TODO: Create type " + std::to_string(create_type) };
            }

            // Rely on normal logic!!! (HACK)
            operation = 0;
            break;
        }
        node = fs_handler.find_in_node(*node, name.substr(i, comp_end - i));
        i = comp_end + 1;
    }


    if (!node)
        return ERROR_OBJECT_NOT_FOUND;

    assert(operation.index() == 0 && std::get<0>(operation) == 0);

    if (!node->lock(access))
        return ERROR_OBJECT_IN_USE;

    return node;
}

void harddisk::impl::fill_file_info(filesystem_handler::node& node, uint32_t fib_cptr)
{
    const auto path = node.path();
    // NOTE: Filename and comment should be BSTR's when returned
    // TODO: datestamp

    mem_.write_u32(fib_cptr + fib_DirEntryType, node.type());
    mem_.write_u32(fib_cptr + fib_EntryType, node.type()); // Must be same as fib_DirEntryType
    mem_.write_u32(fib_cptr + fib_Protection, 0); // Note: bit set means the action is NOT allowed

    const auto size = static_cast<uint32_t>(node.type() == ST_FILE ? file_size(path) : 0);
    const auto ticks = ticks_since_amiga_epoch(last_write_time(path));

    mem_.write_u32(fib_cptr + fib_Size, size);
    mem_.write_u32(fib_cptr + fib_NumBlocks, (size + sector_size_bytes - 1) / sector_size_bytes);
    mem_.write_u32(fib_cptr + fib_Date + 8, ticks % ticks_per_min); // tick
    mem_.write_u32(fib_cptr + fib_Date + 4, (ticks / ticks_per_min) % mins_per_day); // minutes
    mem_.write_u32(fib_cptr + fib_Date, static_cast<uint32_t>(ticks / (ticks_per_min * mins_per_day))); // days
    mem_.write_u8(fib_cptr + fib_Comment, 0);

    std::string name = path.filename().string();
    uint8_t l = static_cast<uint8_t>(name.length());
    if (l > 106)
        l = 106;
    mem_.write_u8(fib_cptr + fib_FileName, l);
    for (int i = 0; i < l; ++i)
        mem_.write_u8(fib_cptr + fib_FileName + 1 + i, name[i]);
    mem_.write_u8(fib_cptr + fib_FileName + 1 + l, 0);
}

uint32_t harddisk::impl::make_lock(filesystem_handler& fs_handler, filesystem_handler::node& node, int32_t access)
{
    assert(access == SHARED_LOCK || access == EXCLUSIVE_LOCK);
    const uint32_t fl = local_ram_alloc(fl_Sizeof);
    if (!fl) {
        node.unlock(access);
        return 0;
    }
    mem_.write_u32(fl + fl_Key, node.id());
    mem_.write_u32(fl + fl_Access, access);
    mem_.write_u32(fl + fl_Task, fs_handler.msg_port_address());
    mem_.write_u32(fl + fl_Volume, BPTR(fs_handler.dos_list_address()));
    return BPTR(fl); // convert to BPTR
}

void harddisk::impl::fill_info_data(filesystem_handler& fs_handler, uint32_t id_cptr)
{
    mem_.write_u32(id_cptr + id_NumSoftErrors, 0);
    mem_.write_u32(id_cptr + id_UnitNumber, 0);
    mem_.write_u32(id_cptr + id_DiskState, ID_VALIDATED);
    mem_.write_u32(id_cptr + id_NumBlocks, 1);
    mem_.write_u32(id_cptr + id_NumBlocksUsed, 1);
    mem_.write_u32(id_cptr + id_BytesPerBlock, sector_size_bytes);
    mem_.write_u32(id_cptr + id_DiskType, ID_DOS_DISK);
    mem_.write_u32(id_cptr + id_VolumeNode, BPTR(fs_handler.dos_list_address()));
    mem_.write_u32(id_cptr + id_InUse, 0);
}

void harddisk::impl::handle_volume_packet()
{
    DosPacket dp {};
    dp.dp_Type = mem_.read_u32(ptr_hold_ + dp_Type);
    dp.dp_Arg1 = mem_.read_u32(ptr_hold_ + dp_Arg1);
    dp.dp_Arg2 = mem_.read_u32(ptr_hold_ + dp_Arg2);
    dp.dp_Arg3 = mem_.read_u32(ptr_hold_ + dp_Arg3);
    dp.dp_Arg4 = mem_.read_u32(ptr_hold_ + dp_Arg4);
    dp.dp_Arg5 = mem_.read_u32(ptr_hold_ + dp_Arg5);
    dp.dp_Arg6 = mem_.read_u32(ptr_hold_ + dp_Arg6);
    dp.dp_Arg7 = mem_.read_u32(ptr_hold_ + dp_Arg7);

    const auto id = mem_.read_u32(ptr_hold_ + dp_Res1);

    if (id >= shared_folders_initialized_) {
        std::cerr << "[HD] Invalid ID in dos packet: " << id << " (not mounted)\n";
        mem_.write_u32(ptr_hold_ + dp_Res1, DOSFALSE);
        mem_.write_u32(ptr_hold_ + dp_Res2, ERROR_DEVICE_NOT_MOUNTED);
        return;
    }

    auto& fs_handler = *shared_folders_[id].fs_handler;

    dp.dp_Res1 = DOSFALSE;
    dp.dp_Res2 = ERROR_ACTION_NOT_KNOWN;
    switch (dp.dp_Type) {
    case ACTION_LOCATE_OBJECT: // 8
        action_locate_object(fs_handler, dp);
        break;
    case ACTION_FREE_LOCK: // 15
        action_free_lock(fs_handler, dp);
        break;
    case ACTION_DELETE_OBJECT: // 16
        action_delete_object(fs_handler, dp);
        break;
    case ACTION_RENAME_OBJECT: // 17
        action_rename_object(fs_handler, dp);
        break;
    case ACTION_COPY_DIR: // 19
        action_copy_dir(fs_handler, dp);
        break;
    case ACTION_SET_PROTECT: // 21
        action_set_protect(fs_handler, dp);
        break;
    case ACTION_CREATE_DIR: // 22
        action_create_dir(fs_handler, dp);
        break;
    case ACTION_EXAMINE_OBJECT: // 23
        action_examine_object(fs_handler, dp);
        break;
    case ACTION_EXAMINE_NEXT: // 24
        action_examine_next(fs_handler, dp);
        break;
    case ACTION_DISK_INFO: // 25
        action_disk_info(fs_handler, dp);
        break;
    case ACTION_INFO: // 26
        action_info(fs_handler, dp);
        break;
    case ACTION_FLUSH: // 27
        dp.dp_Res1 = DOSTRUE;
        dp.dp_Res2 = 0;
        break;
    case ACTION_PARENT: // 29
        action_parent(fs_handler, dp);
        break;
    case ACTION_SAME_LOCK: // 40
        action_same_lock(fs_handler, dp);
        break;
    case ACTION_READ: // 82 ('R')
        action_read(fs_handler, dp);
        break;
    case ACTION_WRITE: // 87 ('W')
        action_write(fs_handler, dp);
        break;
    case ACTION_FINDUPDATE: // 1004
    case ACTION_FINDINPUT: // 1005
    case ACTION_FINDOUTPUT: // 1006
        action_find(fs_handler, dp);
        break;
    case ACTION_END: // 1007
        action_end(fs_handler, dp);
        break;
    case ACTION_SEEK: // 1008
        action_seek(fs_handler, dp);
        break;
    case ACTION_IS_FILESYSTEM: // 1027
        dp.dp_Res1 = DOSTRUE;
        dp.dp_Res2 = 0;
        break;
    default:
#if FS_HANDLER_DEBUG > 0
        std::cout << "[HD] Unhandled DOS packet: " << action_name(dp.dp_Type) << " Args: " << hexfmt(dp.dp_Arg1) << " " << hexfmt(dp.dp_Arg2) << " " << hexfmt(dp.dp_Arg3) << " " << hexfmt(dp.dp_Arg4) << "\n";
#else
        ;
#endif
    }

    mem_.write_u32(ptr_hold_ + dp_Res1, dp.dp_Res1);
    mem_.write_u32(ptr_hold_ + dp_Res2, dp.dp_Res2);
}


void harddisk::impl::action_locate_object(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_LOCATE_OBJECT (8)
    // dp_Argl - BPTR to struct FileLock
    // dp_Arg2 - BPTR to BCPL string (object name)
    // dp_Arg3 - LONG (access mode)
    // dp_Resl - BPTR to struct FileLock
    // dp_Res2 - LONG (AmigaDOS error code if dp_Resl == ZERO

    dp.dp_Res1 = 0;
    const int32_t access = static_cast<int32_t>(dp.dp_Arg3) == EXCLUSIVE_LOCK ? EXCLUSIVE_LOCK : SHARED_LOCK;
    auto lock_res = node_operation(fs_handler, dp.dp_Arg1, dp.dp_Arg2, access);

    if (lock_res.index()) {
        dp.dp_Res1 = make_lock(fs_handler, *std::get<1>(lock_res), access);
        dp.dp_Res2 = NO_ERROR;
        if (!dp.dp_Res1)
            dp.dp_Res2 = ERROR_NO_FREE_STORE;
    } else {
        dp.dp_Res2 = std::get<uint32_t>(lock_res);
    }

#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] Locate object lock=$" << hexfmt(BADDR(dp.dp_Arg1)) << " name \"" << read_bcpl_string(mem_, BADDR(dp.dp_Arg2)) << "\" " << (access == EXCLUSIVE_LOCK ? "exclusive" : "shared") << " -> $" << hexfmt(BADDR(dp.dp_Res1)) << " err " << error_name(dp.dp_Res2) << "\n";
#endif
}

void harddisk::impl::action_free_lock(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_FREE_LOCK (15)
    // dp_Argl - BPTR to struct FileLock
    // dp_Resl - LONG (success code; DOS boolean)
    // dp_Res2 - LONG (AmigaDOS error code if dp_Resl == DOSFALSE)
    const auto lock_cptr = BADDR(dp.dp_Arg1);
#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] Free lock=$" << hexfmt(lock_cptr) << "\n";
#endif

    auto* node = node_from_lock(fs_handler, dp.dp_Arg1);
    if (!node) {
        dp.dp_Res1 = DOSFALSE;
        dp.dp_Res2 = ERROR_INVALID_LOCK;
        return;
    }

    node->unlock(mem_.read_u32(lock_cptr + fl_Access));

    local_ram_free(lock_cptr, fl_Sizeof);

    dp.dp_Res1 = DOSTRUE;
    dp.dp_Res2 = NO_ERROR;
}

void harddisk::impl::action_rename_object(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_RENAME_OBJECT (17)
    // dp_Argl - BPTR to struct FileLock (original)
    // dp_Arg2 - BPTR to BCPL string (original name)
    // dp_Arg3 - BPTR to struct FileLock (new)
    // dp_Arg4 - BPTR to BCPL string (new name)
    // dp_Resl - LONG (success code; DOS boolean)
    // dp.Res2 - LONG (AmigaDOS error code if dp_Resl == DOSFALSE)


    auto lock_res = node_operation(fs_handler, dp.dp_Arg1, dp.dp_Arg2, EXCLUSIVE_LOCK);

    if (lock_res.index()) {
        auto old_node = std::get<1>(lock_res);
        auto new_res = node_operation(fs_handler, dp.dp_Arg3, dp.dp_Arg4, SHARED_LOCK, old_node);
        assert(new_res.index() == 0);
        dp.dp_Res1 = DOSFALSE;
        dp.dp_Res2 = std::get<0>(new_res);
        if (dp.dp_Res2 != 0)
            old_node->unlock(EXCLUSIVE_LOCK);
        else
            dp.dp_Res1 = DOSTRUE;
    } else {
        dp.dp_Res1 = 0;
        dp.dp_Res2 = std::get<0>(lock_res);
    }

#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] Rename lock=$" << hexfmt(BADDR(dp.dp_Arg1)) << " name=\"" << read_bcpl_string(mem_, BADDR(dp.dp_Arg2)) << "\" to lock=$" << hexfmt(BADDR(dp.dp_Arg3)) << " name=\"" << read_bcpl_string(mem_, BADDR(dp.dp_Arg4)) << " -> " << error_name(dp.dp_Res2) << "\n";
#endif
}

void harddisk::impl::action_copy_dir(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_COPY_DIR (19)
    // dp_Argl - BPTR to struct FileLock (original)
    // dp_Resl - BPTR to struct FileLock (duplicate)
    // dp_Res2 - LONG (AmigaDOS error code if dp_Resl == ZERO)
#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] copy lock=$" << hexfmt(BADDR(dp.dp_Arg1)) << "\n";
#endif

    filesystem_handler::node* node = &fs_handler.root_node();
    if (dp.dp_Arg1) {
        auto lock_cptr = BADDR(dp.dp_Arg1);
        if (static_cast<int32_t>(mem_.read_u32(lock_cptr + fl_Access)) != SHARED_LOCK
            || (node = fs_handler.node_from_key(mem_.read_u32(lock_cptr + fl_Key))) == nullptr) {
#if FS_HANDLER_DEBUG > 0
            std::cout << "[HD] Invalid lock duplication\n";
#endif
            dp.dp_Res1 = 0;
            dp.dp_Res2 = ERROR_INVALID_LOCK;
            return;
        }
    }

    if (!node->lock(SHARED_LOCK)) {
        dp.dp_Res1 = 0;
        dp.dp_Res2 = ERROR_OBJECT_IN_USE;
        return;
    }

    dp.dp_Res2 = NO_ERROR;
    dp.dp_Res1 = make_lock(fs_handler, *node, SHARED_LOCK);
    if (!dp.dp_Res1)
        dp.dp_Res2 = ERROR_NO_FREE_STORE;
}

void harddisk::impl::action_delete_object(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_DELETE_OBJECT (16)
    // dp_Argl - BPTR to struct FileLock
    // dp_Arg2 - BPTR to BCPL string (object name)
    // dp_Resl - LONG (success code; DOS boolean)
    // dp_Res2 - LONG (AmigaDOS error code if dp_Resl == DOSFALSE)
    auto lock_res = node_operation(fs_handler, dp.dp_Arg1, dp.dp_Arg2, EXCLUSIVE_LOCK);
    if (lock_res.index()) {
        auto& node = *std::get<1>(lock_res);
        dp.dp_Res1 = DOSTRUE;
        if ((dp.dp_Res2 = fs_handler.delete_node(node)) != NO_ERROR) {
            dp.dp_Res1 = DOSFALSE;
            node.unlock(EXCLUSIVE_LOCK);
        }
    } else {
        dp.dp_Res1 = DOSFALSE;
        dp.dp_Res2 = std::get<0>(lock_res);
    }

#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] Delete lock=$" << hexfmt(BADDR(dp.dp_Arg1)) << " name=\"" << read_bcpl_string(mem_, BADDR(dp.dp_Arg2)) << "\" -> " << error_name(dp.dp_Res2) << "\n";
#endif
}

void harddisk::impl::action_set_protect(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_SET_PROTECT (21)
    // dp_Argl - not used (NULL)
    // dp_Arg2 - BPTR to struct FileLock
    // dp_Arg3 - BPTR to BCPL string (object name)
    // dp_Arg4 - ULONG (bit mask)
    // dp_Resl - LONG (success code; DOS boolean)
    // dp_Res2 - LONG (AmigaDOS error code if dp_Resl == DOSFALSE)

    auto lock_res = node_operation(fs_handler, dp.dp_Arg2, dp.dp_Arg3, EXCLUSIVE_LOCK);
    if (!lock_res.index()) {
        dp.dp_Res1 = DOSFALSE;
        dp.dp_Res2 = std::get<0>(lock_res);
    } else {
        // Fake sucess
        std::get<1>(lock_res)->unlock(EXCLUSIVE_LOCK);
        dp.dp_Res1 = DOSTRUE;
        dp.dp_Res2 = NO_ERROR;
    }
#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] Set protect lock=$" << hexfmt(BADDR(dp.dp_Arg2)) << " object=\"" << read_bcpl_string(mem_, BADDR(dp.dp_Arg3)) << "\" mask=$" << hexfmt(dp.dp_Arg4) << " -> " << error_name(dp.dp_Res2) << "\n";
#endif
}

void harddisk::impl::action_create_dir(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_CREATE_DIR (22)
    // dp_Argl - BPTR to struct FileLock
    // dp_Arg2 - BPTR to BCPL string (directory name)
    // dp_Resl - BPTR to struct FileLock
    // dp_Res2 - LONG (AmigaDOS error code if dp_Resl == ZERO)
    //auto lock_res = lock_node(fs_handler, dp.dp_Arg2, dp.dp_Arg3, EXCLUSIVE_LOCK, ST_USERDIR);


    auto lock_res = node_operation(fs_handler, dp.dp_Arg1, dp.dp_Arg2, EXCLUSIVE_LOCK, ST_USERDIR);

    if (lock_res.index()) {
        dp.dp_Res1 = make_lock(fs_handler, *std::get<1>(lock_res), EXCLUSIVE_LOCK);
        dp.dp_Res2 = NO_ERROR;
        if (!dp.dp_Res1)
            dp.dp_Res2 = ERROR_NO_FREE_STORE;
    } else {
        dp.dp_Res1 = 0;
        dp.dp_Res2 = std::get<0>(lock_res);
    }

#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] Create dir lock=$" << hexfmt(BADDR(dp.dp_Arg1)) << " name=\"" << read_bcpl_string(mem_, BADDR(dp.dp_Arg2)) << "\" -> " << error_name(dp.dp_Res2) << "\n";
#endif
}


void harddisk::impl::action_examine_object(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_EXAMINE_OBJECT (23)
    // dp_Argl - BPTR to struct FileLock
    // dp_Arg2 ~ BPTR to struct FilelnfoBlock
    // dp_Resl - LONG (success code; DOS boolean)
    // dp_Res2 - LONG (AmigaDOS error code if dp_Resl == DOSFALSE)
#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] Examine object lock=$" << hexfmt(BADDR(dp.dp_Arg1)) << " info=$" << hexfmt(BADDR(dp.dp_Arg2)) << "\n";
#endif

    auto* node = node_from_lock(fs_handler, dp.dp_Arg1);
    if (!node) {
        dp.dp_Res1 = DOSFALSE;
        dp.dp_Res2 = ERROR_INVALID_LOCK;
        return;
    }

    const auto fib_cptr = BADDR(dp.dp_Arg2);
    mem_.write_u32(fib_cptr + fib_DiskKey, 0);
    fill_file_info(*node, fib_cptr);

    dp.dp_Res1 = DOSTRUE;
    dp.dp_Res2 = NO_ERROR;
}

void harddisk::impl::action_examine_next(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_EXAMINE_NEXT (24)
    // dp_Argl - BPTR to struct FileLock
    // dp_Arg2 - BPTR to struct FilelnfoBlock
    // dp_Resl - LONG (success code; DOS boolean)
    // dp_Res2 - LONG (AmigaDOS error code if dp_Resl == DOSFALSE)

#if FS_HANDLER_DEBUG > 2
    std::cout << "[HD] Examine next lock=$" << hexfmt(BADDR(dp.dp_Arg1)) << " info=$" << hexfmt(BADDR(dp.dp_Arg2)) << "\n";
#endif

    auto* node = node_from_lock(fs_handler, dp.dp_Arg1);
    if (!node) {
        dp.dp_Res1 = DOSFALSE;
        dp.dp_Res2 = ERROR_INVALID_LOCK;
        return;
    }

    const auto fib_cptr = BADDR(dp.dp_Arg2);

    auto next_node = fs_handler.find_next_node(*node, mem_.read_u32(fib_cptr + fib_DiskKey));

    if (!next_node) {
        dp.dp_Res1 = DOSFALSE;
        dp.dp_Res2 = ERROR_NO_MORE_ENTRIES;
        return;
    }

    mem_.write_u32(fib_cptr + fib_DiskKey, next_node->id());
    fill_file_info(*next_node, fib_cptr);

    dp.dp_Res1 = DOSTRUE;
    dp.dp_Res2 = NO_ERROR;
}

void harddisk::impl::action_disk_info(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_DISKINFO (25)
    // dp_Argl - BPTR to struct InfoData
    // dp_Resl - LONG (success code; DOS boolean)
    // dp_Res2 - LONG (AmigaDOS error code if dp_Resl == DOSFALSE)
#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] Disk info $" << hexfmt(BADDR(dp.dp_Arg1)) << "\n";
#endif

    fill_info_data(fs_handler, BADDR(dp.dp_Arg1));

    dp.dp_Res1 = DOSTRUE;
    dp.dp_Res2 = NO_ERROR;
}

void harddisk::impl::action_info(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_INFO (26)
    // dp_Argl - BPTR to struct FileLock
    // dp_Arg2 - BPTR to struct InfoData
    // dp_Resl - LONG (success code; DOS boolean)
    // dp_Res2 - LONG (AmigaDOS error code if dp_Resl == DOSFALSE)
#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] Info lock=$" << hexfmt(BADDR(dp.dp_Arg1)) << " info=$" << hexfmt(BADDR(dp.dp_Arg2)) << "\n";
#endif

    fill_info_data(fs_handler, BADDR(dp.dp_Arg2));

    dp.dp_Res1 = DOSTRUE;
    dp.dp_Res2 = NO_ERROR;
}

void harddisk::impl::action_parent(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_PARENT (29)
    // dp_Argl - BPTR to struct FileLock (original)
    // dp_Resl - BPTR to struct FileLock (parent lock)
    // dp_Res2 - LONG (AmigaDOS error code if dp_Resl == ZERO)

    // Must return 0/0 if lock is for the root node
#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] Parent lock=$" << hexfmt(BADDR(dp.dp_Arg1)) << "\n";
#endif

    dp.dp_Res1 = 0;
    dp.dp_Res2 = NO_ERROR;
    if (!dp.dp_Arg1)
        return;

    auto node = node_from_lock(fs_handler, dp.dp_Arg1);
    if (!node) {
        dp.dp_Res2 = ERROR_INVALID_LOCK;
        return;
    } else if (node == &fs_handler.root_node()) {
        return;
    }
    node = &node->parent();
    if (!node->lock(SHARED_LOCK)) {
        dp.dp_Res2 = ERROR_OBJECT_IN_USE;
        return;
    }

    dp.dp_Res2 = NO_ERROR;
    dp.dp_Res1 = make_lock(fs_handler, *node, SHARED_LOCK);
    if (!dp.dp_Res1)
        dp.dp_Res2 = ERROR_NO_FREE_STORE;
}

void harddisk::impl::action_same_lock(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_SAME_LOCK (40)
    // dp_Argl - BPTR to struct FileLock
    // dp_Arg2 - BPTR to struct FileLock
    // dp_Resl - LONG (success code; DOS boolean)
    // dp_Res2 - LONG

    auto l1 = node_from_lock(fs_handler, dp.dp_Arg1);
    auto l2 = node_from_lock(fs_handler, dp.dp_Arg2);
    if (!l1 || !l2) {
        dp.dp_Res1 = DOSFALSE;
        dp.dp_Res2 = ERROR_INVALID_LOCK;
    } else {
        dp.dp_Res1 = l1->id() == l2->id() ? DOSTRUE : DOSFALSE;
        dp.dp_Res2 = NO_ERROR;
    }
#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] Same lock " << hexfmt(BADDR(dp.dp_Arg1)) << " " << hexfmt(BADDR(dp.dp_Arg2)) << " -> " << dp.dp_Res1 << " " << error_name(dp.dp_Res2) << "\n";
#endif
}

void harddisk::impl::action_find(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_FINDUPDATE (1004), ACTION_FINDINPUT (1005), or
    // ACTION_FINDOUTPUT (1006)
    // dp_Argl - BPTR to struct FileHandle
    // dp_Arg2 - BPTR to struct FileLock
    // dp_Arg3 - BPTR to BCPL string (file name)
    // dp_Resl - LONG (success code; DOS boolean)
    // dp_Res2 - LONG (AmigaDOS error code if dp_Resl == DOSFALSE)

    const int32_t access = dp.dp_Type == ACTION_FINDINPUT ? SHARED_LOCK : EXCLUSIVE_LOCK;
    auto lock_res = node_operation(fs_handler, dp.dp_Arg2, dp.dp_Arg3, access, dp.dp_Type == ACTION_FINDOUTPUT ? ST_FILE : 0);

    dp.dp_Res1 = DOSFALSE;
    if (lock_res.index()) {
        auto node = std::get<1>(lock_res);
        if (node->type() == ST_FILE) {
            auto mode = dp.dp_Type == ACTION_FINDINPUT ? std::ios::in : dp.dp_Type == ACTION_FINDOUTPUT ? std::ios::out : std::ios::in | std::ios::out;
                                                                                                             
            std::fstream f { node->path().c_str(), mode | std::fstream::binary };
            if (f &&f.is_open()) {
                auto fh_idx = fs_handler.make_file_handle(*node, std::move(f), access);
#if FS_HANDLER_DEBUG > 1
                std::cout << "[HD] Opened " << node->path() << " as " << fh_idx << "\n";
#endif
                mem_.write_u32(BADDR(dp.dp_Arg1) + fh_Arg1, fh_idx);
                dp.dp_Res1 = DOSTRUE;
                dp.dp_Res2 = NO_ERROR;
            } else {
#if FS_HANDLER_DEBUG > 0
                std::cout << "[HD] Failed to open " << node->path() << "\n";
#endif
                node->unlock(access);
                dp.dp_Res2 = ERROR_OBJECT_NOT_FOUND;
            }
        } else {
            node->unlock(access);
            dp.dp_Res2 = ERROR_OBJECT_WRONG_TYPE;
        }
    } else {
        dp.dp_Res2 = std::get<0>(lock_res);
    }

#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] " << action_name(dp.dp_Type) << " filehandle=$" << hexfmt(BADDR(dp.dp_Arg1)) << " lock=$" << hexfmt(BADDR(dp.dp_Arg2)) << " name=\"" << read_bcpl_string(mem_, BADDR(dp.dp_Arg3)) << "\" -> " << error_name(dp.dp_Res2) << "\n";
#endif
}

void harddisk::impl::action_end(filesystem_handler& fs_handler, DosPacket& dp)
{
    //dp_Type - ACTION_END (1007)
    //dp_Argl - fh->fh_Argl
    //dp_Resl - LONG (success code; DOS boolean)
    //dp_Res2 - LONG (AmigaDOS error code if dp_Resl == DOSFALSE)

    if (fs_handler.close_file_handle(dp.dp_Arg1)) {
        dp.dp_Res1 = DOSTRUE;
        dp.dp_Res2 = NO_ERROR;
    } else {
        dp.dp_Res1 = DOSFALSE;
        dp.dp_Res2 = ERROR_BAD_NUMBER;
    }

#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] Close file handle " << dp.dp_Arg1 << " -> " << error_name(dp.dp_Res2) << "\n";    
#endif
}

void harddisk::impl::action_read(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_READ (82 == 'R')
    // dp_Argl - fh->fh_Argl
    // dp_Arg2 - void * (buffer)
    // dp_Arg3 - LONG (number of characters to be read)
    // dp_Resl - LONG (number of characters actually read; O ~ EOF; -1 ~ error)
    // dp_Res2 - LONG (AmigaDOS error code if dp_Resl == -1)

    auto fh = fs_handler.file_handle_from_idx(dp.dp_Arg1);
    if (fh) {
        auto& f = fh->file();
        if (f) {
            dp.dp_Res1 = 0;
            dp.dp_Res2 = NO_ERROR;
            // Not really efficient
            for (uint32_t i = 0; i < dp.dp_Arg3; ++i) {
                const int ch = f.get();
                if (ch == EOF)
                    break;
                mem_.write_u8(dp.dp_Arg2 + i, static_cast<uint8_t>(ch));
                ++dp.dp_Res1;
            }
        } else if (f.eof()) {
            dp.dp_Res1 = 0;
            dp.dp_Res2 = NO_ERROR;
        } else {
            dp.dp_Res1 = static_cast<uint32_t>(-1);
            dp.dp_Res2 = ERROR_SEEK_ERROR; // ??
        }
    } else {
        dp.dp_Res1 = static_cast<uint32_t>(-1);
        dp.dp_Res2 = ERROR_BAD_NUMBER;
    }

#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] Read " << dp.dp_Arg1 << " " << dp.dp_Arg3 << " bytes -> " << error_name(dp.dp_Res2) << " " << static_cast<int32_t>(dp.dp_Res1) << "\n";
#endif
}

void harddisk::impl::action_write(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Type - ACTION_WRITE (87 == 'W')
    // dp_Argl - fh->fh_Argl
    // dp_Arg2 - const void * (buffer)
    // dp_Arg3 - LONG (number of characters to be written)
    // dp_Resl - LONG (number of characters actually written)
    // dp_Res2 - LONG (AmigaDOS error code if dp_Resl <> dp_Arg3)
    auto fh = fs_handler.file_handle_from_idx(dp.dp_Arg1);
    if (fh) {
        auto& f = fh->file();
        dp.dp_Res1 = 0;
        dp.dp_Res2 = NO_ERROR;
        // Not really efficient
        for (uint32_t i = 0; i < dp.dp_Arg3 && f; ++i) {
            f.put(mem_.read_u8(dp.dp_Arg2 + i));
            ++dp.dp_Res1;
        }
        if (!f) {
#if FS_HANDLER_DEBUG > 0
            std::cout << "[HD] Write failed for " << dp.dp_Arg1 << " " << dp.dp_Arg3 << " bytes -> " << error_name(dp.dp_Res2) << " " << static_cast<int32_t>(dp.dp_Res1) << "\n";
#endif
            dp.dp_Res2 = ERROR_DISK_FULL; // ??
        }
    } else {
        dp.dp_Res1 = static_cast<uint32_t>(-1);
        dp.dp_Res2 = ERROR_BAD_NUMBER;
    }

#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] Write " << dp.dp_Arg1 << " " << dp.dp_Arg3 << " bytes -> " << error_name(dp.dp_Res2) << " " << static_cast<int32_t>(dp.dp_Res1) << "\n";
#endif
}

void harddisk::impl::action_seek(filesystem_handler& fs_handler, DosPacket& dp)
{
    // dp_Argl - fh->fh_Argl
    // dp_Arg2 - LONG (position or offset)
    // dp_Arg3 - LONG (seek mode)
    // dp_Resl - LONG (absolute position before this Seek() operation took place)
    // dp_Res2 - LONG (AmigaDOS error code if dp_Resl == -1)

    constexpr uint32_t OFFSET_BEGINNING = static_cast<uint32_t>(-1);
    constexpr uint32_t OFFSET_CURRENT = 0;
    constexpr uint32_t OFFSET_END = 1;

    if (auto fh = fs_handler.file_handle_from_idx(dp.dp_Arg1); fh) {
        auto& f = fh->file();
        // TODO: What should happen for output files?
        // TODO: Check for valid range
        f.clear(); // clear eof etc.
        dp.dp_Res1 = static_cast<uint32_t>(f.tellg());
        dp.dp_Res2 = NO_ERROR;

        auto mode = std::ios::beg;
        switch (dp.dp_Arg3) {
        case OFFSET_BEGINNING:
            break;
        case OFFSET_CURRENT:
            mode = std::ios::cur;
            break;
        case OFFSET_END:
            mode = std::ios::end;
            break;
        default:
            goto bad_number;
        }

        f.seekg(static_cast<int32_t>(dp.dp_Arg2), mode);
    } else {
bad_number:
        dp.dp_Res1 = static_cast<uint32_t>(-1);
        dp.dp_Res2 = ERROR_BAD_NUMBER;
    }

#if FS_HANDLER_DEBUG > 1
    std::cout << "[HD] Seek " << dp.dp_Arg1 << " pos " << static_cast<int32_t>(dp.dp_Arg2) << " mode " << static_cast<int32_t>(dp.dp_Arg3) << " -> " << error_name(dp.dp_Res2) << "\n";
#endif
}

harddisk::harddisk(memory_handler& mem, bool& cpu_active, const bool_func& should_disable_autoboot, const std::vector<std::string>& hdfilenames, const std::vector<std::string>& shared_folders)
    : impl_{ new impl(mem, cpu_active, should_disable_autoboot, hdfilenames, shared_folders) }
{
}

harddisk::~harddisk() = default;

autoconf_device& harddisk::autoconf_dev()
{
    return *impl_;
}
