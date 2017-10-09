# Project_0 report by 20120421 민병욱

## team name(no)
38

## number of token used
0

## contribution
이번 플젝은 둘이 같이해서 기여도가 다른 부분이 없습니다.


## our-strategy

저희가 착안한 점은 threads에서 빌드 이후에 pintos run으로 테스트 가능한 것들이

tests/threads/ 디렉토리에 그 소스들이 있다는 것에서 시작했습니다.

예를 들어 pintos run alarm-zero이 실행되는 것은

tests/threads/alarm-zero.c 가 있고

tests/threads/tests.c 와 tests/threads/tests.h에 그에 해당하는 함수 test_alarm_zero가 선언되어 있고

tests/threads/Make.tests엔 각 c파일들의 경로가 있었습니다.



따라서 저희는 tests/threads/hello.c에 새로운 void test_hello(void) 함수를 작성하였고

tests/threads/Make.tests에 hello.c를 컴파일 할 수 있게 아래처럼 추가하고

tests/threads/tests.c와 tests/threads/tests.h에 각각 필요한 선언을 추가해줬습니다.

```shell
(in tests/threads/Make.tests)
// let 'make' command also compile my new hello.c
tests/threads_SRC += tests/threads/hello.c

(in tests/threads/tests.c)
static const struct test tests[] =
{
    // to let 'pintos run hello' possible, calling test_hello function in hello.c
    {"hello", test_hello},
    ...

(in tests/threads/tests.h)
// declare test_hello from hello.c
extern test_func test_hello;

```

