# 2017PintosProject

## About

This is my own Pintos Project in KAIST CS330 Operating System course.

Please check standford pintos manual from [here](https://web.stanford.edu/class/cs140/projects/pintos/pintos.html)

My codes only works on Linux and doesn't support any other OS's such as Windows, Mac.

Be aware that our KAIST pintos project's initial skeleton codes differ from standford's.

You can checkout to the skeleton codes commit from my initial commit (project 0)

```shell
git clone https://github.com/phraust1612/2017PintosProject.git
cd 2017PintosProject/
git checkout a51afad3b7efe218a7a07a9fad8f7ee4dbe24442
```

## Install

You need some pre-requisites to run my pintos codes well.

* Pintos codes
* Bochs

### Pintos

You can easily get my pintos codes via git command.

```shell
git clone https://github.com/phraust1612/2017PintosProject.git
```

Be sure to SET a new PATH.

You can just add below codes at the end of your ~/.bashrc

```shell
export PATH=$PATH:<directory where you git cloned>/2017PintosProject/src/utils/
```

Of course the codes will differ depending on the directory where you cloned.

There should appear some help messages when you type 'pintos' on your shell

if your PATH's set successfully.

### Bochs

Please download bochs 2.2.6 version from [here](https://sourceforge.net/projects/bochs/files/bochs/2.2.6/)

Then type below.

```shell
env SRCDIR=(your bochs.tar.gz dir) PINTOSDIR=(your git dir)/2017PintosProject/src/utils/pintos DSTDIR=(download destination dir) sh bochs-2.2.6-build.sh
```

## Test

You can run all tests at src/threads/ and src/userprog/ by typing

```shell
make clean && make check
```

Each corresponds to project 1 and 2.

As our course didn't require me to pass mlfqs series tests of project 1,

this code would fail for only those cases.

To run single test code, first type 'make' in src/threads/ or src/userprog/

Here is an example of executing project 1 test.

```shell
pintos -v -- -q run alarm-multiple
```

and for project 2 test,

```shell
pintos -v --fs-disk=2 -p build/tests/userprog/open-normal -a open-normal -p ../tests/userprog/sample.txt -a sample.txt -- -f -q run open-normal
```

You must pass all files (including executable ELF file and any file using in user program) via -p option and name them via -a option.

You can see test codes at src/tests/threads/ and src/tests/userprog/

