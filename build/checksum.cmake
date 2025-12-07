
  execute_process(
    COMMAND  cmake -E sha256sum /home/houssemhammami/deeprey-template/build/deepreytemplate-1.0.0.0+2512072256.13b7209_debian-x86_64-12-x86_64.tar.gz
    OUTPUT_FILE /home/houssemhammami/deeprey-template/build/deepreytemplate-1.0.0.0+2512072256.13b7209_debian-x86_64-12-x86_64.sha256
  )
  file(READ /home/houssemhammami/deeprey-template/build/deepreytemplate-1.0.0.0+2512072256.13b7209_debian-x86_64-12-x86_64.sha256 _SHA256)
  string(REGEX MATCH "^[^ ]*" checksum ${_SHA256} )
  configure_file(
    /home/houssemhammami/deeprey-template/build/deepreytemplate-1.0-debian-x86_64-12.xml.in
    /home/houssemhammami/deeprey-template/build/deepreytemplate-1.0-debian-x86_64-12.xml
    @ONLY
  )
