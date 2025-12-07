#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <direct.h>  // For _chdir

#define MAX_FILES 2048
#define MAX_PATH_LEN 32767  // Max Windows path with \\?\ prefix

// Convert wchar_t to char
void wchar_to_char(const wchar_t *wstr, char *str, int str_size) {
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, str_size, NULL, NULL);
}

// Convert char to wchar_t
void char_to_wchar(const char *str, wchar_t *wstr, int wstr_size) {
    MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, wstr_size);
}

// Check if path is a directory
int is_directory(const char *path) {
    wchar_t wpath[MAX_PATH_LEN];
    char_to_wchar(path, wpath, MAX_PATH_LEN);
    DWORD attr = GetFileAttributesW(wpath);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

// Check if filename matches filters
int matches_filters(const char *filename, int argc, char *argv[], int startIndex) {
    if (argc - startIndex <= 0) return 1; // no filters
    for (int i = startIndex; i < argc; i++)
        if (strstr(filename, argv[i]) != NULL)
            return 1;
    return 0;
}

// Check if string is numeric
int is_number(const char *s) {
    if (!s || *s == '\0') return 0;
    for (; *s; s++)
        if (!isdigit(*s)) return 0;
    return 1;
}

// Clear console
void clear_console() {
    system("cls");
}

// Remove trailing slashes from a path
void remove_trailing_slash(char *path) {
    size_t len = strlen(path);
    
    // Remove all trailing slashes
    while (len > 0 && (path[len - 1] == '\\' || path[len - 1] == '/')) {
        path[len - 1] = '\0';
        len--;
    }
    
    // Don't remove trailing slash from root directories like "C:\"
    if (len > 0 && path[len - 1] == ':') {
        strcat(path, "\\");
    }
}

// Set current working directory
int set_cur_dir(const char *path) {
    if (_chdir(path) == 0) {
        return 1;
    }
    return 0;
}

// Scan directory and fill file list
int scan_directory(const char *dirpath, char *files[], int argc, char *argv[], int filterStart) {
    WIN32_FIND_DATAW fd;
    wchar_t searchPath[MAX_PATH_LEN];
    wchar_t wdirpath[MAX_PATH_LEN];
    
    char_to_wchar(dirpath, wdirpath, MAX_PATH_LEN);
    _snwprintf(searchPath, MAX_PATH_LEN, L"%s\\*", wdirpath);

    HANDLE hFind = FindFirstFileW(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    int count = 0;
    do {
        const wchar_t *wname = fd.cFileName;
        if (wcscmp(wname, L".") == 0 || wcscmp(wname, L"..") == 0) continue;

        char name[MAX_PATH_LEN];
        wchar_to_char(wname, name, MAX_PATH_LEN);

        if (matches_filters(name, argc, argv, filterStart) && count < MAX_FILES)
            files[count++] = _strdup(name);

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return count;
}

// Print documentation
void print_documentation() {
    clear_console();
    printf("Better Toolbar CLI\n");
    printf("Navigate folders, open files, supports absolute paths.\n");
    printf("Usage:\n");
    printf("  better-toolbar.exe [folder] [filters...]\n\n");
    printf("Examples:\n");
    printf("  better-toolbar.exe C:\\Users\\Documents\n");
    printf("  better-toolbar.exe . .txt .pdf\n");
    printf("  better-toolbar.exe D:\\Projects .cpp .h\n");
}

// Main function
int main(int argc, char *argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    char dirpath[MAX_PATH_LEN];
    int filterStart = 1;

    // Determine scanning directory
    if (argc >= 2) {
        char candidatePath[MAX_PATH_LEN];
        
        // Copy the argument
        strncpy(candidatePath, argv[1], MAX_PATH_LEN - 1);
        candidatePath[MAX_PATH_LEN - 1] = '\0';
        remove_trailing_slash(candidatePath);

        // Check if it's a directory
        if (is_directory(candidatePath)) {
            // Valid directory - try to change to it
            if (set_cur_dir(candidatePath)) {
                // Success - get the absolute path and use it
                wchar_t wdirpath[MAX_PATH_LEN];
                GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
                wchar_to_char(wdirpath, dirpath, MAX_PATH_LEN);
                remove_trailing_slash(dirpath);
                filterStart = 2; // Filters start at argv[2]
            } else {
                // Failed to change directory
                printf("Error: Cannot access directory '%s'\n", candidatePath);
                // Use current directory as fallback
                wchar_t wdirpath[MAX_PATH_LEN];
                GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
                wchar_to_char(wdirpath, dirpath, MAX_PATH_LEN);
                remove_trailing_slash(dirpath);
                filterStart = 1; // Treat argv[1] as filter
            }
        } else {
            // Not a directory - use current dir, treat argv[1] as filter
            wchar_t wdirpath[MAX_PATH_LEN];
            GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
            wchar_to_char(wdirpath, dirpath, MAX_PATH_LEN);
            remove_trailing_slash(dirpath);
            filterStart = 1;
        }
    } else {
        // No arguments - use current directory
        wchar_t wdirpath[MAX_PATH_LEN];
        GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
        wchar_to_char(wdirpath, dirpath, MAX_PATH_LEN);
        remove_trailing_slash(dirpath);
    }

    char *files[MAX_FILES];
    int fileCount = 0;

    while (1) {
        // Cleanup previous scan results
        for (int i = 0; i < fileCount; i++) free(files[i]);
        fileCount = scan_directory(dirpath, files, argc, argv, filterStart);

        clear_console();
        printf("Current directory: %s\n", dirpath);

        if (fileCount == 0)
            printf("No matching files found.\n");
        else {
            printf("Found files:\n");
            for (int i = 0; i < fileCount; i++)
                printf("[%d] %s\n", i, files[i]);
        }

        printf("\nEnter index, 'up' to go up, d/D for docs, q/Q to quit: ");
        char input[64];
        if (!fgets(input, sizeof(input), stdin)) continue;
        input[strlen(input)-1] = '\0'; // strip newline

        if (strlen(input) == 1) {
            if (input[0] == 'q' || input[0] == 'Q') break;
            if (input[0] == 'd' || input[0] == 'D') {
                print_documentation();
                printf("\nPress Enter to continue...");
                getchar(); 
                continue;
            }
        }

        if (_stricmp(input, "up") == 0) {
            char tempPath[MAX_PATH_LEN];
            strcpy(tempPath, dirpath);
            
            char *lastSlash = strrchr(tempPath, '\\');
            if (!lastSlash) lastSlash = strrchr(tempPath, '/');
            
            if (lastSlash) {
                *lastSlash = '\0';
                
                // If we're at a drive root, stay there
                if (strlen(tempPath) > 0 && tempPath[strlen(tempPath) - 1] == ':') {
                    strcat(tempPath, "\\");
                }
                
                // Try to change to parent directory
                if (set_cur_dir(tempPath)) {
                    wchar_t wdirpath[MAX_PATH_LEN];
                    GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
                    wchar_to_char(wdirpath, dirpath, MAX_PATH_LEN);
                    remove_trailing_slash(dirpath);
                }
            }
            // If no slash found, we're already at root - stay here
            continue;
        }

        if (!is_number(input)) {
            printf("Invalid input!\n");
            Sleep(1000);
            continue;
        }

        int index = atoi(input);
        if (index < 0 || index >= fileCount) {
            printf("Index out of range!\n");
            Sleep(1000);
            continue;
        }

        char fullPath[MAX_PATH_LEN];
        snprintf(fullPath, MAX_PATH_LEN, "%s\\%s", dirpath, files[index]);

        if (is_directory(fullPath)) {
            // Change scanning directory to subdirectory
            if (set_cur_dir(fullPath)) {
                wchar_t wdirpath[MAX_PATH_LEN];
                GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
                wchar_to_char(wdirpath, dirpath, MAX_PATH_LEN);
                remove_trailing_slash(dirpath);
            }
            continue;
        }

        // Open file with default application
        wchar_t wfullPath[MAX_PATH_LEN];
        char_to_wchar(fullPath, wfullPath, MAX_PATH_LEN);
        ShellExecuteW(NULL, L"open", wfullPath, NULL, NULL, SW_SHOWNORMAL);
    }

    // Final cleanup
    for (int i = 0; i < fileCount; i++) free(files[i]);
    printf("Exiting.\n");
    return 0;
}