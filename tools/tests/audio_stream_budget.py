#!/usr/bin/env python3
"""Execute production live-stream admission and cleanup with counted VFS/decoder ownership."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = (ROOT / 'src/sound/OpenAL/AL_SoundSample.cpp').read_text()
    production = source[source.index('static uint64 soundStreamBytes'):source.index('/*\n========================\nRoQAudioDelta')]
    harness = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include "src/sound/SoundMemoryBudget.h"
#undef INT_MAX
using uint64=uint64_t;
template<class T> T Max(T a,T b){return std::max(a,b);}
struct idMath { static const int INT_MAX=2147483647; static int ClampInt(int a,int b,int c){return std::clamp(c,a,b);} };
struct Cvar { int value=8; int GetInteger() const{return value;} } s_streamMemoryMB;
struct idLib { static void Printf(const char*,...){} static void Warning(const char*,...){} };
static bool noFile=false,badDecoder=false;
static SoundMemoryBudget aggregate;
static uint64 aggregateLimit=64*1024*1024;
static bool SoundMemoryAcquire(uint64 bytes) { return aggregate.Acquire(bytes,aggregateLimit); }
static void SoundMemoryRelease(uint64 bytes) { aggregate.Release(bytes); }
static int files=0,decoders=0;
enum { FS_SEEK_SET };
struct idFile {
    idFile(){++files;} ~idFile(){--files;}
    int Length(){return 64;} int Tell(){return 0;}
    int Seek(unsigned,int){return 0;} int Read(void*,int bytes){return bytes;}
};
struct VFS { idFile* OpenFileRead(const char*){return noFile?nullptr:new idFile;} void CloseFile(idFile* file){delete file;} } vfs;
static VFS* fileSystem=&vfs;
struct stb_vorbis_openq4_granule_page { unsigned page_start, page_end, granule; };
struct SoundVorbisStream {
    static constexpr int ArenaBytes=2*1024*1024, ChunkFrames=4096;
    char cache[64*1024];
    SoundVorbisStream(){++decoders;} ~SoundVorbisStream(){--decoders;}
    bool Open(int(*)(void*,unsigned,void*,int),void*,int){return !badDecoder;}
    int Error(){return 1;} bool Seek(unsigned){return true;}
    bool SetCountedLength(unsigned frames,const stb_vorbis_openq4_granule_page& page){return frames && page.page_end>page.page_start;}
    int Read(short*,int count){return count;} int Channels(){return 1;} int Rate(){return 22050;} unsigned Frames(){return 22050;}
};
class idSoundOggStream {
public:
    idSoundOggStream(); ~idSoundOggStream(); bool Open(const char*); void Close();
    bool Seek(unsigned); bool SetCountedLength(unsigned,unsigned,unsigned,unsigned);
    int Read(short*,int); int Channels()const; int Rate()const; unsigned Frames()const;
    static void PrintMemoryInfo();
    idFile* file; SoundVorbisStream* decoder;
};
@PRODUCTION@
int main(){
    idSoundOggStream a,b,c,d;
    assert(a.Open("a") && b.Open("b") && c.Open("c"));
    assert(!d.Open("full") && files==3 && decoders==3 && soundStreamRejected==1);
    assert(soundStreamBytes==3*SOUND_STREAM_BYTES && soundStreamPeakBytes<=8*1024*1024);
    b.Close(); assert(d.Open("after stop"));
    a.Close(); c.Close(); d.Close();
    assert(files==0 && decoders==0 && soundStreamBytes==0);
    assert(aggregate.Used()==0);
    aggregateLimit=SOUND_STREAM_BYTES-1;
    assert(!a.Open("aggregate cap") && files==0 && decoders==0 && aggregate.Used()==0);
    aggregateLimit=64*1024*1024;
    noFile=true; assert(!a.Open("missing")); noFile=false;
    badDecoder=true; assert(!a.Open("corrupt")); badDecoder=false;
    assert(files==0 && decoders==0 && soundStreamBytes==0);
    s_streamMemoryMB.value=32;
    idSoundOggStream more[8]; for(auto& stream:more) assert(stream.Open("live"));
    s_streamMemoryMB.value=8;
    assert(!a.Open("budget reduced"));
    for(auto& stream:more) stream.Close();
    assert(a.Open("retry") && files==1 && decoders==1);
    assert(a.Open("replace") && files==1 && decoders==1 && soundStreamBytes==SOUND_STREAM_BYTES);
    a.Close(); a.Close();
    assert(files==0 && decoders==0 && soundStreamBytes==0);
    assert(!a.SetCountedLength(22050,0,100,11025));
    assert(a.Open("counted") && a.SetCountedLength(22050,0,100,11025) && !a.SetCountedLength(0,0,100,11025));
    a.Close(); assert(files==0 && decoders==0 && soundStreamBytes==0);
    std::puts("PASS: stream byte budget, saturation, stop/retry, VFS/decoder failures, replacement and budget reduction");
}
'''.replace('@PRODUCTION@', production)
    scratch = ROOT / '.tmp/audio/stream-budget'
    scratch.mkdir(parents=True, exist_ok=True)
    cpp, exe = scratch / 'test.cpp', scratch / ('test.exe' if os.name == 'nt' else 'test')
    cpp.write_text(harness)
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(cpp), '-o', str(exe)], cwd=scratch, check=True)
    subprocess.run([str(exe)], cwd=scratch, check=True)


if __name__ == '__main__':
    main()
