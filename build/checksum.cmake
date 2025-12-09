
  execute_process(
    COMMAND  cmake -E sha256sum /home/houssemhammami/deeprey-grib/build/deeprey_grib-1.0.0.0+2512092208.4ecb94c_debian-x86_64-12-x86_64.tar.gz
    OUTPUT_FILE /home/houssemhammami/deeprey-grib/build/deeprey_grib-1.0.0.0+2512092208.4ecb94c_debian-x86_64-12-x86_64.sha256
  )
  file(READ /home/houssemhammami/deeprey-grib/build/deeprey_grib-1.0.0.0+2512092208.4ecb94c_debian-x86_64-12-x86_64.sha256 _SHA256)
  string(REGEX MATCH "^[^ ]*" checksum ${_SHA256} )
  configure_file(
    /home/houssemhammami/deeprey-grib/build/deeprey_grib-1.0-debian-x86_64-12.xml.in
    /home/houssemhammami/deeprey-grib/build/deeprey_grib-1.0-debian-x86_64-12.xml
    @ONLY
  )
