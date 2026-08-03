#include <cstdio>
#include <cstring>


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
    memset(tls_object.buffer, 1, sizeof tls_object.buffer);

    printf("[foo] tls address = %p\n", (void*)&tls_object);
}
