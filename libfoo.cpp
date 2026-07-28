#include <cstdio>


struct LargeTLS
{
    char buffer[4 * 1024 * 1024];

    LargeTLS()  { printf("[TLS] constructor\n"); }
    ~LargeTLS() { printf("[TLS] destructor\n"); }
};


thread_local LargeTLS tls_object;


extern "C"
void foo()
{
    printf("[foo] tls address = %p\n", (void*)&tls_object);
}
