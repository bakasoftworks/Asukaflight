add_library(AccessDenied STATIC
  access_denied.c aes.c)

target_compile_definitions(AccessDenied
  PRIVATE CBC=0
  PRIVATE ECB=1
  PRIVATE CTR=0
  )

target_compile_options(AccessDenied PRIVATE -O3)
