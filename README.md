# 2017PintosProject

## About

This is my own Pintos Project in KAIST CS330 Operating System course.  
Please check standford pintos manual from [here](https://web.stanford.edu/class/cs140/projects/pintos/pintos.html)  

My codes only works on Linux and doesn't support any other OS's such as Windows, Mac.  
For more details about the environment, please check out [here](https://github.com/hangpark/pintos-dev-env-kaist)  

Be aware that our KAIST pintos project's initial skeleton codes differ from standford's.  
Download skeleton codes from [here](https://github.com/hangpark/pintos-dev-env-kaist/raw/master/pintos.tar.gz)  

Since we didn't follow 4.4BSD Scheduler,  
this project fails only 7 tests (tests/threads/mlfqs-...) of project 1  
and fails 1 test (tests/vm/page-merge-mm) of project 4  
Except those 8, it passes all remaining tests of whole projects.  

## Install

You need some pre-requisites to run my pintos codes well.

* Ubuntu 8.04 (recommended)
* GCC 3.4
* Bochs 2.2.6

If you are familiar with Docker, please use this [docker image](https://github.com/hangpark/pintos-dev-env-kaist)  

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

Threre are 4 projects and each corresponds to below directories.  
* src/threads/
* src/userprog/
* src/vm/
* src/filesys/

To run all tests, type below command in one of the project directories.  

```shell
make clean && make check
```

To run a single test, make (your test name).result in build directory.  
Here's an example for args-none test of userprog project.  
```shell
make
cd build/
make tests/userprog/args-none.result
```

For more details about tests, please check out pintos [manual](https://web.stanford.edu/class/cs140/projects/pintos/pintos.html)  

