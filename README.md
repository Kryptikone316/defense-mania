# defense mania

> A crossplatform tower defense game built with processing.

## Donations

If you like Defense Mania and want to support further development of software like this, buy me a beer by [donating via PayPal](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=FC3GNGS5XD792). <3

## Installation

**Windows, Linux, Mac OSX**: Download the latest release for your platform from [here](https://github.com/DefenseMania/defense-mania/releases).

**Android**: Download the latest release from [here](https://github.com/DefenseMania/defense-mania/releases) or install it from the [Google PlayStore](https://play.google.com/store/apps/details?id=processing.test.android).

**Web/Browser**: Just start the game [here](https://defensemania.github.io/).

## Prerequisites

* [Java](http://www.oracle.com/technetwork/java/javase/downloads/jre7-downloads-1880261.html) (JRE) is required to run the native **Windows, Linux and Mac OSX** versions.
* **Android** 2.3.3 Gingerbread is required to run DefenseMania and at least 256-512 MB of RAM.
* Your Browser should support the [canvas](http://caniuse.com/#feat=canvas) element.

## How to play the game

1. Click on "Start", choose a map, a difficulty and start the game.
2. Click anywhere on the map to build a tower. The hammer icon on the bottom left shows green areas where you can build towers.
3. When you build the first tower, the game will start.
4. Build towers to prevent the "enemies" from reaching the end of the path.
5. Click on a tower to upgrade, sell or repair it. Selling a tower gives you about 80% of the money you paid for it in the first place.

**Windows, Linux, MacOSX**: Navigation on the map holding your right mouse button. Zoom in with your mouse wheel.

**Android**: Drag to navigation the map, pinch to zoom in.

**Web/Browser**: Navigation on the map holding your right mouse button. Zooming is disabled.

## Contribute

### Game

##### prerequisites

* [Processing 2.03b](https://github.com/processing/processing/releases/tag/processing-2.0b3)
* [Android SDK](https://developer.android.com/sdk/index.html#Other)
* [Apache Ant](https://ant.apache.org/manual/install.html)
* [GIT](https://git-scm.com/downloads)
* [UglifyJs](https://github.com/mishoo/UglifyJS#install-npm)
* [PhantomJs](http://phantomjs.org/download.html)

##### getting started

* Download and install everything above
* Install Android SDK **API Level 10** Platform & Build-tools **Rev. 19.1**
* **The game was built on Processing 2.03b and won't run on the current version of Processing. We will hopefully fix that soon.**
* Get the source: `git clone https://github.com/kriskbx/defensemania.git`

##### directory structure

DefenseMania is splitted into 3 parts: desktop application and android application, browser/web application. They share most of the code. The shared code is located in `src/shared` as well as all the assets. Everything is symlinked (relatively) into the 3 parts of DefenseMania. If you want to recreate the symlinks, run `./src/symlinks.sh`.

##### build it

1. Desktop: open the desktop project in processing and export the application.
2. Android: open processing, export the android project from the file menu, run `cd src/android && ant`, this will create the file `src/android/application.android/DefenseMania-unsigned.apk`.
3. Browser: run `cd src/browser && ant`, this will create the compiled and minified project in `src/browser/application.web`.

### Maps & Map Editor

1. Download the DM Map Editor from [here](https://github.com/DefenseMania/defense-mania-map-editor/releases) and read [how to use it](https://github.com/DefenseMania/defense-mania-map-editor#how-to-use-the-editor).
2. You can submit new maps via pull-requests in **this** repository.

### Graphics

Take a look [here](https://github.com/DefenseMania/defense-mania-graphics).

## Datafrog SF3500 port (branch `sf3500`)

`sf3500/` holds a native C++ port for the Datafrog SF3500 (TreeFrogUI, MIPS32r2, SDL 1.2): the game logic is a translation of the
Processing code in `src/shared/code`, reading the same XML/PNG data in `src/shared/data` (plus a few additions: DejaVu fonts, 18 synthesised
sound effects, two extra maps, The Swamp redrawn and The Tundra). It uses the device's own display, audio and gamepad modules, so it needs
only SDL 1.2 and a classic SDL_ttf.

```text
MIPS_PREFIX=/path/to/mips-mti-linux-gnu- SYSROOT=/path/to/sysroot SDL_TTF_DIR=/path/to/SDL_ttf-1.2.11 ./build_sf3500.sh
```

`defensemania.cfg` (picture tuning, volumes, frame cap, endless-mode growth) is read from the port folder. `tools/` has the scripts that
made the sound effects, the loudness table for the optional music pack, and the two generated maps. The notes on how a rare one-frame
display glitch was traced (`slot_check` in the cfg, `sf3500/slotcheck.cpp`) are in the comments of those files.
