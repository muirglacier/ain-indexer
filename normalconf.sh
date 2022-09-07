export BDB_PREFIX='/home/anonymous/Development/ain/db4'
./configure --prefix="$(pwd)/depends/x86_64-pc-linux-gnu" CXXFLAGS="-O3 -march=native" CFLAGS="-O3 -march=native" BDB_LIBS="-L${BDB_PREFIX}/lib -ldb_cxx-4.8" BDB_CFLAGS="-I${BDB_PREFIX}/include" --enable-static
