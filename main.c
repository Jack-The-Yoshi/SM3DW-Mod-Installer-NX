// main.c — SM3DW GameBanana Mod Installer NX

#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <switch/applets/swkbd.h>
#include "miniz.h"

#include <curl/curl.h>
#include "jsmn.h"

#include <stdarg.h>

#define TITLE_ID               "010028600EBDA000"
#define TARGET_BASE            "sdmc:/atmosphere/contents/" TITLE_ID
#define TARGET_ROMFS           TARGET_BASE "/romfs"
#define TARGET_EXEFS           TARGET_BASE "/exefs"

#define APP_DIR                "sdmc:/switch/SM3DWModInstaller"
#define CACHE_DIR              APP_DIR "/cache"
#define DOWNLOADS_DIR          APP_DIR "/downloads"
#define EXTRACT_DIR            APP_DIR "/_work/extracted"

#define MAX_PATH_LEN           1024
#define MAX_MODS_PER_PAGE      40
#define MAX_FILES_PER_MOD      30
#define UI_VISIBLE             18

static int endsWithIgnoreCase(const char *s, const char *suffix);
static void clearScreen(void);
static int jsoneq(const char *json, const jsmntok_t *tok, const char *s);

PadState pad;

static int show_search_keyboard(char *outBuf, size_t outSize)
{
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);

    if (R_FAILED(rc))
        return -1;

    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, "Enter mod search term");
    swkbdConfigSetHeaderText(&kbd, "Search GameBanana Mods");
    swkbdConfigSetInitialText(&kbd, "");
    swkbdConfigSetStringLenMax(&kbd, outSize - 1);

    rc = swkbdShow(&kbd, outBuf, outSize);

    swkbdClose(&kbd);

    if (R_FAILED(rc))
        return -1;

    if (strlen(outBuf) == 0)
        return -1;

    return 0;
}

typedef struct {
    const char *name;
    int sectionId;
} ModCategory;

static ModCategory categories[] = {
    {"Plessy", 12445},
    {"GUI", 2323},
    {"Textures", 950},
    {"Effects", 1503},
    {"Maps", 6809},
    {"Game Files", 4685},
    {"Skins", 11210},
};

#define CATEGORY_COUNT (sizeof(categories)/sizeof(categories[0]))

static int category_select_screen(void)
{
    int sel = 0;

    while (appletMainLoop()) {

        padUpdate(&pad);
        u64 kd = padGetButtonsDown(&pad);

        if (kd & HidNpadButton_Plus)
            return -1;

        if (kd & HidNpadButton_Down) {
            if (sel < CATEGORY_COUNT - 1)
                sel++;
        }

        if (kd & HidNpadButton_Up) {
            if (sel > 0)
                sel--;
        }

        if (kd & HidNpadButton_A) {
            return categories[sel].sectionId;
        }

        clearScreen();
        printf("SM3DW Mod Installer NX\n\n");
        printf("Select Category:\n\n");

        for (int i = 0; i < CATEGORY_COUNT; i++) {
            printf("%c %s\n",
                (i == sel) ? '>' : ' ',
                categories[i].name);
        }

        printf("\nA = select | + = exit\n");

        consoleUpdate(NULL);
        svcSleepThread(10000000);
    }

    return -1;
}

// -----------------------------
// HTTP (curl) to memory / file
// -----------------------------
typedef struct {
    char *data;
    size_t size;
} MemBuf;

static size_t curl_write_mem(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    MemBuf *mb = (MemBuf*)userdata;
    char *p = (char*)realloc(mb->data, mb->size + total + 1);
    if (!p) return 0;
    mb->data = p;
    memcpy(mb->data + mb->size, ptr, total);
    mb->size += total;
    mb->data[mb->size] = 0;
    return total;
}

static size_t curl_write_file(void *ptr, size_t size, size_t nmemb, void *userdata) {
    return fwrite(ptr, size, nmemb, (FILE*)userdata);
}

typedef struct {
    long long dltotal;
    long long dlnow;
} DlProgress;

static int curl_xferinfo(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    DlProgress *pr = (DlProgress*)p;
    pr->dltotal = (long long)dltotal;
    pr->dlnow = (long long)dlnow;
    return 0;
}

static int http_get_to_mem(const char *url, MemBuf *out) {
    out->data = NULL;
    out->size = 0;

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SM3DWModInstallerNX/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_mem);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);

    // Reasonable timeouts
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(out->data);
        out->data = NULL;
        out->size = 0;
        return -1;
    }
    return 0;
}

static int http_download_file(const char *url, const char *outPath, DlProgress *progress) {
    if (progress) { progress->dltotal = 0; progress->dlnow = 0; }

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    FILE *f = fopen(outPath, "wb");
    if (!f) { curl_easy_cleanup(curl); return -1; }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SM3DWModInstallerNX/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);

    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L); // allow big files

    // Progress
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_xferinfo);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, progress);

    CURLcode res = curl_easy_perform(curl);

    fclose(f);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) ? 0 : -1;
}

// -----------------------------
// FS helpers
// -----------------------------
static void clearScreen(void) { printf("\x1b[2J\x1b[H"); }

static int pathExists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static int isDir(const char *p) {
    struct stat st;
    if (stat(p, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

static void mkdir_p(const char *path) {
    char tmp[MAX_PATH_LEN];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return;
    strncpy(tmp, path, sizeof(tmp)-1);
    tmp[sizeof(tmp)-1] = 0;

    // If ends with '/', keep it, else mkdir will fail on file path later, so we handle parents separately too.
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static void ensure_parent_dir(const char *filePath) {
    char parent[MAX_PATH_LEN];
    strncpy(parent, filePath, sizeof(parent)-1);
    parent[sizeof(parent)-1] = 0;
    char *slash = strrchr(parent, '/');
    if (!slash) return;
    *slash = 0;
    mkdir_p(parent);
}

static int removeTree(const char *path) {
    DIR *d = opendir(path);
    if (!d) return 0;

    struct dirent *e;
    char child[MAX_PATH_LEN];

    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);

        if (isDir(child)) {
            removeTree(child);
            rmdir(child);
        } else {
            remove(child);
        }
    }
    closedir(d);
    return 0;
}

static int copyFile(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;

    ensure_parent_dir(dst);
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }

    char buf[64*1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return -1; }
    }
    fclose(in);
    fclose(out);
    return 0;
}

static int copyTree(const char *srcDir, const char *dstDir) {
    DIR *d = opendir(srcDir);
    if (!d) return -1;

    mkdir_p(dstDir);

    struct dirent *e;
    char s[MAX_PATH_LEN], t[MAX_PATH_LEN];

    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(s, sizeof(s), "%s/%s", srcDir, e->d_name);
        snprintf(t, sizeof(t), "%s/%s", dstDir, e->d_name);

        if (isDir(s)) {
            if (copyTree(s, t) != 0) { closedir(d); return -1; }
        } else {
            if (copyFile(s, t) != 0) { closedir(d); return -1; }
        }
    }

    closedir(d);
    return 0;
}

static int equalsIgnoreCase(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static int findFolderRecursive(const char *root, const char *wanted, char *outPath, size_t outSz) {
    DIR *d = opendir(root);
    if (!d) return 0;

    struct dirent *e;
    char child[MAX_PATH_LEN];

    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(child, sizeof(child), "%s/%s", root, e->d_name);

        if (isDir(child)) {
            if (equalsIgnoreCase(e->d_name, wanted)) {
                snprintf(outPath, outSz, "%s", child);
                closedir(d);
                return 1;
            }
            if (findFolderRecursive(child, wanted, outPath, outSz)) {
                closedir(d);
                return 1;
            }
        }
    }

    closedir(d);
    return 0;
}

// -----------------------------
// ZIP extraction (miniz)
// -----------------------------

static int is_abs_or_dotdot(const char *p) {
    if (!p || !*p) return 1;
    if (p[0] == '/') return 1;
    if (isalpha((unsigned char)p[0]) && p[1] == ':') return 1;
    if (strstr(p, "..") != NULL) return 1;
    return 0;
}

static int extractArchiveTo(const char *archivePath, const char *destDir, char *err, size_t errsz) {

    if (!endsWithIgnoreCase(archivePath, ".zip")) {
        snprintf(err, errsz, "Only ZIP archives are supported.");
        return -1;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, archivePath, 0)) {
        snprintf(err, errsz, "Failed to open ZIP file.");
        return -1;
    }

    int fileCount = (int)mz_zip_reader_get_num_files(&zip);

    for (int i = 0; i < fileCount; i++) {

        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st))
            continue;

        if (mz_zip_reader_is_file_a_directory(&zip, i))
            continue;

        if (is_abs_or_dotdot(st.m_filename))
            continue;

        char outPath[MAX_PATH_LEN];
        snprintf(outPath, sizeof(outPath), "%s/%s", destDir, st.m_filename);

        ensure_parent_dir(outPath);

        if (!mz_zip_reader_extract_to_file(&zip, i, outPath, 0)) {
            mz_zip_reader_end(&zip);
            snprintf(err, errsz, "Extraction failed.");
            return -1;
        }
    }

    mz_zip_reader_end(&zip);
    err[0] = 0;
    return 0;
}

typedef struct {
    int id;
    char name[256];
    char description[512];

    char author[128];
    char version[64];

    int likeCount;
    int viewCount;
} ModItem;

typedef struct {
    char filename[256];
    char url[512];
    int sizeBytes;
} ModFile;

static void json_unescape_slashes(char *s)
{
    // Turns "https:\/\/x\/y" into "https://x/y"
    // Only handles the \/ case (which is what GameBanana uses here)
    for (char *p = s; *p; )
    {
        if (p[0] == '\\' && p[1] == '/')
        {
            memmove(p, p + 1, strlen(p)); // delete the backslash
            // don't advance p, we want to re-check at this position
        }
        else
        {
            p++;
        }
    }
}

static int fetch_mod_metadata(int modId, ModItem *mod)
{
    char url[512];

    snprintf(url, sizeof(url),
        "https://gamebanana.com/apiv11/Mod/%d?"
        "_csvProperties=_sVersion,_nLikeCount,_nViewCount,_aSubmitter",
        modId);

    MemBuf mb = {0};
    if (http_get_to_mem(url, &mb) != 0)
        return -1;

    jsmn_parser p;
    jsmntok_t tokens[4096];
    jsmn_init(&p);

    int tokenCount = jsmn_parse(&p, mb.data, mb.size, tokens, 4096);
    if (tokenCount < 0) {
        free(mb.data);
        return -1;
    }

    for (int i = 0; i < tokenCount; i++)
    {
        if (jsoneq(mb.data, &tokens[i], "_sVersion"))
        {
            jsmntok_t *val = &tokens[i + 1];
            int len = val->end - val->start;
            if (len > 63) len = 63;

            memcpy(mod->version, mb.data + val->start, len);
            mod->version[len] = 0;
        }
        else if (jsoneq(mb.data, &tokens[i], "_nLikeCount"))
        {
            jsmntok_t *val = &tokens[i + 1];
            char buf[32];
            int len = val->end - val->start;
            if (len > 31) len = 31;

            memcpy(buf, mb.data + val->start, len);
            buf[len] = 0;
            mod->likeCount = atoi(buf);
        }
        else if (jsoneq(mb.data, &tokens[i], "_nViewCount"))
        {
            jsmntok_t *val = &tokens[i + 1];
            char buf[32];
            int len = val->end - val->start;
            if (len > 31) len = 31;

            memcpy(buf, mb.data + val->start, len);
            buf[len] = 0;
            mod->viewCount = atoi(buf);
        }
        else if (jsoneq(mb.data, &tokens[i], "_aSubmitter"))
        {
            int subPairs = tokens[i + 1].size;
            int subIndex = i + 2;

            for (int s = 0; s < subPairs; s++)
            {
                if (jsoneq(mb.data, &tokens[subIndex], "_sName"))
                {
                    jsmntok_t *val = &tokens[subIndex + 1];
                    int len = val->end - val->start;
                    if (len > 127) len = 127;

                    memcpy(mod->author, mb.data + val->start, len);
                    mod->author[len] = 0;
                }
                subIndex += 2;
            }
        }
    }

    free(mb.data);
    return 0;
}

static int fetch_full_description(int modId, char *outDesc, size_t outSize)
{
    char url[512];

    snprintf(url, sizeof(url),
        "https://gamebanana.com/apiv11/Mod/%d?_csvProperties=_sDescription",
        modId);

    MemBuf mb = {0};
    if (http_get_to_mem(url, &mb) != 0)
        return -1;

    jsmn_parser p;
    jsmntok_t tokens[4096];
    jsmn_init(&p);

    int tokenCount = jsmn_parse(&p, mb.data, mb.size, tokens, 4096);
    if (tokenCount < 0) {
        free(mb.data);
        return -1;
    }

    for (int i = 0; i < tokenCount; i++)
    {
        if (tokens[i].type == JSMN_STRING &&
            strncmp(mb.data + tokens[i].start, "_sDescription", 13) == 0)
        {
            jsmntok_t *val = &tokens[i + 1];

            int len = val->end - val->start;
            if (len >= (int)outSize)
                len = outSize - 1;

            memcpy(outDesc, mb.data + val->start, len);
            outDesc[len] = 0;

            json_unescape_slashes(outDesc);

            free(mb.data);
            return 0;
        }
    }

    free(mb.data);
    return -1;
}

static int fetch_mod_files_api_v11(int modId, ModFile *outFiles, int maxFiles)
{
    char url[512];

    snprintf(url, sizeof(url),
        "https://gamebanana.com/apiv11/Mod/%d?_csvProperties=_aFiles",
        modId);

    MemBuf mb = {0};
    if (http_get_to_mem(url, &mb) != 0)
        return -1;

    jsmn_parser p;
    jsmntok_t tokens[4096];
    jsmn_init(&p);

    int tokenCount = jsmn_parse(&p, mb.data, mb.size, tokens, 4096);
    if (tokenCount < 0) {
        free(mb.data);
        return -1;
    }

    int count = 0;

    for (int i = 0; i < tokenCount && count < maxFiles; i++) {

        if (tokens[i].type == JSMN_STRING &&
            strncmp(mb.data + tokens[i].start, "_sFile", 6) == 0)
        {
            int valueIndex = i + 1;
            int len = tokens[valueIndex].end - tokens[valueIndex].start;
            if (len > 255) len = 255;

            memcpy(outFiles[count].filename,
                   mb.data + tokens[valueIndex].start,
                   len);
            outFiles[count].filename[len] = 0;
        }

        if (tokens[i].type == JSMN_STRING &&
            strncmp(mb.data + tokens[i].start, "_sDownloadUrl", 13) == 0)
        {
            int valueIndex = i + 1;
            int len = tokens[valueIndex].end - tokens[valueIndex].start;
            if (len > 511) len = 511;

            memcpy(outFiles[count].url,
                   mb.data + tokens[valueIndex].start,
                   len);
            outFiles[count].url[len] = 0;
			
			json_unescape_slashes(outFiles[count].url);

            count++;
        }
    }

    free(mb.data);
    return count;
}

static int jsoneq(const char *json, const jsmntok_t *tok, const char *s)
{
    if (tok->type != JSMN_STRING) return 0;
    int len = tok->end - tok->start;
    return (int)strlen(s) == len &&
           strncmp(json + tok->start, s, len) == 0;
}

static int skip_token(jsmntok_t *tokens, int index)
{
    int i, j;

    switch (tokens[index].type)
    {
        case JSMN_PRIMITIVE:
        case JSMN_STRING:
            return index + 1;

        case JSMN_OBJECT:
            i = index + 1;
            for (j = 0; j < tokens[index].size; j++) {
                i = skip_token(tokens, i); // key
                i = skip_token(tokens, i); // value
            }
            return i;

        case JSMN_ARRAY:
            i = index + 1;
            for (j = 0; j < tokens[index].size; j++)
                i = skip_token(tokens, i);
            return i;

        default:
            return index + 1;
    }
}

static int fetch_mods_api_v11(int sectionId, int page, ModItem *outMods, int maxMods)
{
    char url[1024];

    snprintf(url, sizeof(url),
        "https://gamebanana.com/apiv11/Mod/Index?"
        "_nPerpage=%d"
        "&_nPage=%d"
        "&_aFilters[Generic_Category]=%d"
        "&_csvProperties=_idRow,_sName",
        maxMods,
        page,
        sectionId);

    MemBuf mb = {0};
    if (http_get_to_mem(url, &mb) != 0)
        return -1;

    jsmn_parser p;
    jsmntok_t tokens[8192];
    jsmn_init(&p);

    int tokenCount = jsmn_parse(&p, mb.data, mb.size, tokens, 8192);
    if (tokenCount < 0) {
        free(mb.data);
        return -1;
    }

    int recordsIndex = -1;

    for (int i = 0; i < tokenCount; i++) {
        if (jsoneq(mb.data, &tokens[i], "_aRecords")) {
            recordsIndex = i + 1;
            break;
        }
    }

    if (recordsIndex < 0 || tokens[recordsIndex].type != JSMN_ARRAY) {
        free(mb.data);
        return 0;
    }

    int count = 0;
    int idx = recordsIndex + 1;

    for (int r = 0; r < tokens[recordsIndex].size && count < maxMods; r++)
    {
        if (tokens[idx].type != JSMN_OBJECT)
            break;

        int objectIndex = idx;
        int objectEnd = tokens[objectIndex].end;

        int id = 0;
        char name[256] = {0};

        int inner = objectIndex + 1;

        while (inner < tokenCount && tokens[inner].start < objectEnd)
        {
            if (jsoneq(mb.data, &tokens[inner], "_idRow"))
            {
                jsmntok_t *val = &tokens[inner + 1];
                char buf[32];
                int len = val->end - val->start;
                if (len > 31) len = 31;

                memcpy(buf, mb.data + val->start, len);
                buf[len] = 0;
                id = atoi(buf);
            }
            else if (jsoneq(mb.data, &tokens[inner], "_sName"))
            {
                jsmntok_t *val = &tokens[inner + 1];
                int len = val->end - val->start;
                if (len > 255) len = 255;

                memcpy(name, mb.data + val->start, len);
                name[len] = 0;
            }

            inner = skip_token(tokens, inner);
        }

        if (id > 0 && name[0])
        {
            outMods[count].id = id;
            strncpy(outMods[count].name, name,
                    sizeof(outMods[count].name) - 1);
            count++;
        }

        idx = skip_token(tokens, objectIndex);
    }

    free(mb.data);
    return count;
}

static int endsWithIgnoreCase(const char *s, const char *suffix) {
    size_t sl = strlen(s), su = strlen(suffix);
    if (su > sl) return 0;
    const char *p = s + (sl - su);
    for (size_t i = 0; i < su; i++) {
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)suffix[i])) return 0;
    }
    return 1;
}

// -----------------------------
// Install pipeline
// -----------------------------
static int install_archive_file(const char *archivePath) {
    // reset extract dir
    if (pathExists(EXTRACT_DIR)) {
        removeTree(EXTRACT_DIR);
        rmdir(EXTRACT_DIR);
    }
    mkdir_p(EXTRACT_DIR);

    char err[256];
    clearScreen();
    printf("Extracting...\n\n%s\n", archivePath);
    consoleUpdate(NULL);

    if (extractArchiveTo(archivePath, EXTRACT_DIR, err, sizeof(err)) != 0) {
        printf("\n\nERROR: %s\n", err);
        printf("\nOnly ZIP archives are supported.\n");
        return -1;
    }

    char romfsPath[MAX_PATH_LEN] = {0};
    char exefsPath[MAX_PATH_LEN] = {0};

    int hasRomfs = findFolderRecursive(EXTRACT_DIR, "romfs", romfsPath, sizeof(romfsPath));
    int hasExefs = findFolderRecursive(EXTRACT_DIR, "exefs", exefsPath, sizeof(exefsPath));

    clearScreen();
    printf("Scan results:\n");
    printf("romfs: %s\n", hasRomfs ? romfsPath : "(not found)");
    printf("exefs: %s\n\n", hasExefs ? exefsPath : "(not found)");

    if (!hasRomfs && !hasExefs) {
        printf("No romfs/exefs folder found in the archive.\n");
        return -1;
    }

    if (hasRomfs) {
        printf("Installing romfs -> %s\n", TARGET_ROMFS);
        if (copyTree(romfsPath, TARGET_ROMFS) != 0) {
            printf("Failed copying romfs.\n");
            return -1;
        }
    }
    if (hasExefs) {
        printf("Installing exefs -> %s\n", TARGET_EXEFS);
        if (copyTree(exefsPath, TARGET_EXEFS) != 0) {
            printf("Failed copying exefs.\n");
            return -1;
        }
    }

    printf("\nDone.\nInstalled to:\n%s\n", TARGET_BASE);
    return 0;
}

// -----------------------------
// UI helpers
// -----------------------------
static void waitAorPlus(void) {
    printf("\nPress A to continue. (+ to exit)\n");

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);

        if (k & HidNpadButton_A) break;
        if (k & HidNpadButton_Plus) break;

        consoleUpdate(NULL);
        svcSleepThread(10000000);
    }
}

static void ensure_dirs(void) {
    mkdir_p(APP_DIR);
    mkdir_p(CACHE_DIR);
    mkdir_p(DOWNLOADS_DIR);
    mkdir_p(TARGET_BASE);
    mkdir_p(TARGET_ROMFS);
    mkdir_p(TARGET_EXEFS);
}

static int contains_ignore_case(const char *text, const char *search)
{
    if (!text || !search) return 0;

    size_t searchLen = strlen(search);
    if (searchLen == 0) return 1;

    for (size_t i = 0; text[i]; i++)
    {
        size_t j = 0;
        while (text[i + j] &&
               tolower((unsigned char)text[i + j]) ==
               tolower((unsigned char)search[j]))
        {
            j++;
            if (j == searchLen)
                return 1;
        }
    }

    return 0;
}

// -----------------------------
// Main screens
// -----------------------------
static int mod_list_screen(int sectionId)
{
    int page = 1;
    int sel = 0;

    char searchQuery[256] = {0};
    int searchMode = 0;

    while (appletMainLoop())
    {
        ModItem allMods[MAX_MODS_PER_PAGE];
        ModItem mods[MAX_MODS_PER_PAGE];

        memset(allMods, 0, sizeof(allMods));
        memset(mods, 0, sizeof(mods));

        clearScreen();
        printf("SM3DW Mod Installer NX\n");
        printf("Target: %s\n\n", TARGET_BASE);

        if (searchMode)
            printf("Search: \"%s\" (page %d)\n\n", searchQuery, page);
        else
            printf("Category Mods (page %d)\n\n", page);

        consoleUpdate(NULL);

        int fetchedCount = fetch_mods_api_v11(sectionId,
                                              page,
                                              allMods,
                                              MAX_MODS_PER_PAGE);

        if (fetchedCount < 0)
            fetchedCount = 0;

        int modCount = 0;

        if (searchMode)
        {
            for (int i = 0; i < fetchedCount; i++)
            {
                if (contains_ignore_case(allMods[i].name, searchQuery))
                {
                    mods[modCount++] = allMods[i];
                }
            }
        }
        else
        {
            memcpy(mods, allMods, sizeof(ModItem) * fetchedCount);
            modCount = fetchedCount;
        }

        if (sel >= modCount)
            sel = (modCount > 0) ? (modCount - 1) : 0;

        while (appletMainLoop())
        {
            padUpdate(&pad);
            u64 kd = padGetButtonsDown(&pad);

            if (kd & HidNpadButton_Plus)
                return 0;

            if (kd & HidNpadButton_L)
            {
                if (page > 1)
                {
                    page--;
                    sel = 0;
                    break;
                }
            }

            if (kd & HidNpadButton_R)
            {
                page++;
                sel = 0;
                break;
            }

            if (kd & HidNpadButton_X)
            {
                break;
            }

            if (kd & HidNpadButton_Y)
            {
                if (searchMode)
                {
                    searchMode = 0;
                    searchQuery[0] = 0;
                    page = 1;
                    sel = 0;
                    break;
                }
                else
                {
                    if (show_search_keyboard(searchQuery,
                                             sizeof(searchQuery)) == 0)
                    {
                        if (strlen(searchQuery) > 0)
                        {
                            searchMode = 1;
                            page = 1;
                            sel = 0;
                            break;
                        }
                    }
                }
            }

            if (modCount == 0)
            {
                clearScreen();
                printf("SM3DW Mod Installer NX\n\n");

                if (searchMode)
                    printf("No search results found.\n\n");
                else
                    printf("No mods found.\n\n");

                printf("Y = search | L/R = page | X = refresh | + = exit\n");
                consoleUpdate(NULL);
                svcSleepThread(10000000);
                continue;
            }

            if (kd & HidNpadButton_Down)
            {
                if (sel < modCount - 1)
                    sel++;
            }

            if (kd & HidNpadButton_Up)
            {
                if (sel > 0)
                    sel--;
            }

            if (mods[sel].version[0] == 0)
            {
                fetch_mod_metadata(mods[sel].id, &mods[sel]);
            }

            if (kd & HidNpadButton_A)
            {
                int modId = mods[sel].id;

                ModFile files[MAX_FILES_PER_MOD];
                memset(files, 0, sizeof(files));

                clearScreen();
                printf("Loading files...\n");
                printf("Mod: %s\n", mods[sel].name);
                printf("ID: %d\n\n", modId);
                consoleUpdate(NULL);

                int fileCount = fetch_mod_files_api_v11(modId,
                                                        files,
                                                        MAX_FILES_PER_MOD);

                if (fileCount < 0)
                    fileCount = 0;

                int fileSel = 0;

                while (appletMainLoop())
                {
                    padUpdate(&pad);
                    u64 k2 = padGetButtonsDown(&pad);

                    if (k2 & HidNpadButton_Plus)
                        return 0;

                    if (k2 & HidNpadButton_B)
                        break;

                    if (fileCount == 0)
                    {
                        clearScreen();
                        printf("Mod: %s\n\n", mods[sel].name);
                        printf("No downloadable files found.\n");
                        printf("Press B to go back.\n");
                        consoleUpdate(NULL);
                        svcSleepThread(10000000);
                        continue;
                    }

                    if (k2 & HidNpadButton_Down)
                    {
                        if (fileSel < fileCount - 1)
                            fileSel++;
                    }

                    if (k2 & HidNpadButton_Up)
                    {
                        if (fileSel > 0)
                            fileSel--;
                    }

                    if (k2 & HidNpadButton_A)
                    {
                        const ModFile *f = &files[fileSel];

                        char outPath[MAX_PATH_LEN];
                        snprintf(outPath, sizeof(outPath),
                                 "%s/%d_%s",
                                 DOWNLOADS_DIR,
                                 modId,
                                 f->filename);

                        DlProgress pr = {0};

                        clearScreen();
                        printf("Downloading...\n\n");
                        printf("%s\n\n", f->filename);
                        consoleUpdate(NULL);

                        int dlRes = http_download_file(f->url,
                                                       outPath,
                                                       &pr);

                        if (dlRes != 0)
                        {
                            printf("\nDownload failed.\n");
                            consoleUpdate(NULL);
                            svcSleepThread(1500000000);
                            continue;
                        }

                        clearScreen();
                        printf("Download complete:\n\n%s\n\n", outPath);
                        printf("Installing...\n");
                        consoleUpdate(NULL);

                        install_archive_file(outPath);

                        printf("\nPress B to go back.\n");
                        consoleUpdate(NULL);

                        while (appletMainLoop())
                        {
                            padUpdate(&pad);
                            u64 k3 = padGetButtonsDown(&pad);
                            if (k3 & HidNpadButton_B)
                                break;
                            if (k3 & HidNpadButton_Plus)
                                return 0;
                            svcSleepThread(10000000);
                        }
                    }

                    clearScreen();
                    printf("Mod: %s\n\n", mods[sel].name);
                    printf("Files:\n\n");

                    for (int i = 0; i < fileCount; i++)
                    {
                        printf("%c %s\n",
                               (i == fileSel) ? '>' : ' ',
                               files[i].filename);
                    }

                    printf("\n-----------------------------\n");

                    char fullDesc[2048] = {0};

                    if (fetch_full_description(modId,
                                               fullDesc,
                                               sizeof(fullDesc)) == 0)
                    {
                        printf("Description:\n\n");
                        printf("%s\n", fullDesc);
                    }
                    else
                    {
                        printf("Description: (not available)\n");
                    }

                    printf("\nA = download | B = back | + = exit\n");
                    consoleUpdate(NULL);
                    svcSleepThread(10000000);
                }
            }

            clearScreen();
            printf("SM3DW Mod Installer NX\n");

            if (searchMode)
                printf("Search: \"%s\" (page %d) - %d results\n\n",
                       searchQuery,
                       page,
                       modCount);
            else
                printf("Category Mods (page %d) - %d mods loaded\n\n",
                       page,
                       modCount);

            printf("A = open | Y = search | L/R = page | X = refresh | + = exit\n\n");

            int start = 0;
            if (sel >= UI_VISIBLE)
                start = sel - (UI_VISIBLE - 1);

            int end = start + UI_VISIBLE;
            if (end > modCount)
                end = modCount;

            for (int i = start; i < end; i++)
            {
                printf("%c %s\n",
                       (i == sel) ? '>' : ' ',
                       mods[i].name);

                if (i == sel)
                {
                    printf("   Author: %s\n",
                           mods[i].author[0] ? mods[i].author : "Unknown");

                    printf("   Version: %s\n",
                           mods[i].version[0] ? mods[i].version : "N/A");

                    printf("   Likes: %d   Views: %d\n\n",
                           mods[i].likeCount,
                           mods[i].viewCount);
                }
                else
                {
                    printf("\n");
                }
            }

            printf("Showing %d-%d of %d\n",
                   start + 1,
                   end,
                   modCount);

            consoleUpdate(NULL);
            svcSleepThread(10000000);
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
	
	socketInitializeDefault();

    consoleInit(NULL);
    ensure_dirs();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    clearScreen();
    printf("SM3DW Mod Installer NX\n\n");
    printf("This will install romfs/exefs to:\n%s\n\n", TARGET_BASE);
    printf("Press A to continue.\n");
    consoleUpdate(NULL);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kd = padGetButtonsDown(&pad);

        if (kd & HidNpadButton_Plus) break;
        if (kd & HidNpadButton_A) break;

        svcSleepThread(10000000);
    }

    while (appletMainLoop()) {

        int sectionId = category_select_screen();
        if (sectionId <= 0)
            break;  // user pressed +

        mod_list_screen(sectionId);
    }


    curl_global_cleanup();
    consoleExit(NULL);
    socketExit();
    return 0;
}