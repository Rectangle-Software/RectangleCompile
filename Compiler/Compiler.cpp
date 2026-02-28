#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <iostream>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "SimpleArchive.h"

namespace fs = std::filesystem;

void copyFolder(const fs::path& src, const fs::path& dst) {
    fs::create_directories(dst);
    for (auto& entry : fs::recursive_directory_iterator(src)) {
        fs::path rel = fs::relative(entry.path(), src);
        fs::path target = dst / rel;
        if (entry.is_directory())
            fs::create_directories(target);
        else
            fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
    }
}

bool compileLuaFile(lua_State* L, const fs::path& luaFile) {
    if (luaL_loadfile(L, luaFile.string().c_str()) != LUA_OK) {
        std::cout << "Failed to load " << luaFile << ": " << lua_tostring(L, -1) << "\n";
        lua_pop(L, 1);
        return false;
    }
    fs::path outFile = luaFile;
    outFile.replace_extension(".luac");
    std::ofstream out(outFile, std::ios::binary);
    if (!out) return false;
    auto writer = [](lua_State*, const void* p, size_t sz, void* ud) -> int {
        std::ofstream* f = (std::ofstream*)ud;
        f->write((const char*)p, sz);
        return 0;
        };
    if (lua_dump(L, writer, &out, 1) != 0) {
        lua_pop(L, 1);
        return false;
    }
    lua_pop(L, 1);
    fs::remove(luaFile);
    return true;
}

bool compileLuaFolder(const fs::path& folder) {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    bool mainExists = false;
    for (auto& entry : fs::recursive_directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".lua") continue;
        if (entry.path().filename() == "main.lua") mainExists = true;
        if (!compileLuaFile(L, entry.path())) {
            lua_close(L);
            return false;
        }
    }
    lua_close(L);
    return mainExists;
}

int main(int argc, char* argv[]) {
    if (argc < 3) return 1;
    fs::path sourceFolder = argv[1];
    fs::path outputFile = argv[2];
    fs::path iconFile;
    if (argc >= 4) iconFile = argv[3];
    if (!fs::exists(sourceFolder) || !fs::is_directory(sourceFolder)) return 1;
    fs::path compiledFolder = "compiled";
    if (fs::exists(compiledFolder)) fs::remove_all(compiledFolder);
    copyFolder(sourceFolder, compiledFolder);
    if (!compileLuaFolder(compiledFolder)) return 1;
    fs::path archiveFile = "archive.bin";
    std::vector<fs::path> files;
    for (auto& entry : fs::recursive_directory_iterator(compiledFolder))
        if (entry.is_regular_file()) files.push_back(entry.path());
    SimpleArchive::packFiles(compiledFolder, files, archiveFile);
    fs::path stub = "WindowsClient.exe";
    if (!fs::exists(stub)) return 1;
    std::ifstream stubFile(stub, std::ios::binary);
    std::ifstream archiveIn(archiveFile, std::ios::binary | std::ios::ate);
    if (!stubFile || !archiveIn) return 1;
    std::ofstream outExe(outputFile, std::ios::binary);
    outExe << stubFile.rdbuf();
    archiveIn.seekg(0, std::ios::beg);
    outExe << archiveIn.rdbuf();
    uint32_t archiveSize = (uint32_t)archiveIn.tellg();
    outExe.write(reinterpret_cast<char*>(&archiveSize), sizeof(archiveSize));
    fs::path luaDll = "lua53.dll";
    if (fs::exists(luaDll)) fs::copy_file(luaDll, outputFile.parent_path() / luaDll.filename(), fs::copy_options::overwrite_existing);
    if (!iconFile.empty() && fs::exists(iconFile)) {
        std::string rcFile = "temp_icon.rc";
        std::ofstream rc(rcFile);
        rc << "1 ICON \"" << iconFile.string() << "\"";
        rc.close();
        std::string resCmd = "windres " + rcFile + " -O coff -o temp_icon.o";
        std::system(resCmd.c_str());
        std::string editCmd = "link /NOENTRY /OUT:\"" + outputFile.string() + "\" temp_icon.o /INCREMENTAL:NO /SUBSYSTEM:WINDOWS /MERGE:.rsrc=.rdata";
        std::system(editCmd.c_str());
        fs::remove("temp_icon.rc");
        fs::remove("temp_icon.o");
    }
    return 0;
}