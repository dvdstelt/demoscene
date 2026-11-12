// The art folder: high-resolution redraws of MUSTARD.PAK's pictures and restored frames of the video clips, shown
// unless --original-art. Ctx::tex() and Ctx::video() (demo.cpp) use them where the scenes read the pictures and
// frames as colours; re/notes-pictures.md lists every picture, how the original reads it, and which ones a redraw
// can replace.
//
// Pictures: art/<name>.png (or .jpg), <name> = the PAK file's name without ".raw" ("final", "tex3d", "bd2"). The
// redraw covers the file's whole pixel grid at its own resolution (a stacked file such as tex3d.raw: all six tiles,
// stacked as in the file; the remake cuts tile k out of the redraw as rows k/n .. (k+1)/n). RGB or RGBA (straight
// alpha); grey files (no palette) are read as their luminance.
//
// Video: art/video/<CLIP>/NNNN.png (or .jpg), <CLIP> = ALEX, HARL, VS or PARANOID, NNNN = the frame's index in
// <CLIP>.LZW from 0000, one file per original frame, any resolution with the original frame's 5:3 aspect (160x96).
// Frames without a file keep the decoded original frame.
#pragma once
#include <future>
#include <map>
#include <memory>
#include <string>

#include "assets.h"

class ArtStore {
public:
    // dir: the art folder ("" = none); on = false for --original-art. Lists the folder and starts decoding every
    // picture (and the first frames of every clip) on worker threads.
    void init(const std::string& dir, bool on);
    bool on() const { return on_; }
    const std::string& dir() const { return dir_; }

    // The redraw of PAK picture <name> (without ".raw"), RGBA, or nullptr when art is off or there is none.
    // Decoded once; the pointer stays valid until release().
    const Image* picture(const std::string& name);
    bool hasPicture(const std::string& name) const;

    // The restored frame `frame` of clip (ALEX, HARL, VS, PARANOID), RGBA, or nullptr when art is off or that frame
    // has no file. Waits for the frame and decodes the next two ahead; valid until the next call for this clip.
    const Image* videoFrame(const std::string& clip, int frame);
    int videoFiles(const std::string& clip) const;  // restored frames present for clip

    void release();

private:
    struct Decoded {
        Image im;
        std::string err;
    };
    struct Clip {
        std::map<int, std::string> files;               // frame -> path
        std::map<int, std::future<Decoded>> pending;    // decodes running or done, not yet taken
        int current = -1;
        Image currentImage;
    };
    static Decoded decode(const std::string& path);
    void prefetch(Clip& c, int frame);

    std::string dir_;
    bool on_ = false;
    std::map<std::string, std::string> picturePaths_;                // name -> path
    std::map<std::string, std::future<Decoded>> picturePending_;
    std::map<std::string, std::unique_ptr<Image>> pictures_;         // decoded (nullptr entry: unreadable)
    std::map<std::string, Clip> clips_;
};
