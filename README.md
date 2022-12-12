Argos
=====

Argos is a project to make a smart home monitoring system using low-cost
wifi cameras. Cheap ESP32 camera modules are ubiquitous on [Amazon][1].
By using multiple cameras setup around my apartment and some simple object
detection, I should be able to create a text-only description of my apartment.
This is privacy-preserving, easily compressible, and easy to process with simple
routines that can detect situations (e.x. "When someone enters the living room,
turn on the lights.").

The standard esp32 [camera demo firmware][2] is flashed on the camera modules. You'll also need a tool to flash code onto the ESP32.

You need a desktop to run the base software included in this repository.

Project Status
--------------

Development on this project is currently on hold. At the moment, I have object detection working using Yolov4 (Darknet), and the client connects and live-detects objects on the camera. No work on apartment mapping, camera localization, or event detection has been done. Software is provided as-is. Sometime in the future, I may migrate object detection to [Plasticity, my own machine learning framework][3] and add live apartment mapping.

Using a from-scratch model trained in Plasticity was tricky. While I can get decent accuracy on some image recognition datasets, the ESP32 camera data is low-resolution and low contrast, making the model detect all sorts of spurious objects. Using Yolov4 for now as a temporary workaround.

Screenshot
----------

Sorry for the poor image quality, I have since moved, and haven't yet setup the camera in my new apartment. I only have access to old photos of the software, which I took with my phone while standing in front of the camera.

![](images/screenshot.png?raw=true)

Setup & Building
----------------

### First Time

Argos uses [Bazel](https://bazel.build/) as its build system. I personally
recommend downloading [Bazelisk](https://github.com/bazelbuild/bazelisk) and using that to manage the version of Bazel on your machine. It takes some extra work, but manages Bazel updates for you so that you can always be up to date. Also, it's compatible with .bazelversion files, so that projects can specify an exact version of Bazel to use for the build (to solve BUILD compatibility issues).

Install bazelisk with something like...

```
npm install -g @bazel/bazelisk
```

Now fetch the submodule dependencies...

```
git submodule init
git submodule update
```

And make sure Nvidia Cuda is installed.

Then build & run the desktop software with:

```
bazel run host:host_client
```


[1]: https://www.amazon.com/HiLetgo-ESP32-CAM-Development-Bluetooth-Raspberry/dp/B07RXPHYNM#:~:text=ESP32%2DCAM%20is%20a%20WIFI%2B,bit%20CPU%20for%20application%20processors
[2]: https://github.com/espressif/esp32-camera
[3]: https://github.com/jsharf/plasticity