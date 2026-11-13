#ifdef DEBUG

#ifdef __cplusplus
extern "C"
{
#endif
    void assert_func_play(const char* file, int line);
#ifdef __cplusplus
}
#endif

#undef assert
#define assert(x) (x ? (void)0 : assert_func_play(__FILE__, __LINE__))

#else

#undef assert
#define assert(x) (void)0

#endif
