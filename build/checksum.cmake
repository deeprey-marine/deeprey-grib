
  execute_process(
    COMMAND  cmake -E sha256sum /home/deeprey/projects/deeprey-template/build/deepreytemplate-1.0.0.0+2512051103._debian-x86_64-12-x86_64.tar.gz
    OUTPUT_FILE /home/deeprey/projects/deeprey-template/build/deepreytemplate-1.0.0.0+2512051103._debian-x86_64-12-x86_64.sha256
  )
  file(READ /home/deeprey/projects/deeprey-template/build/deepreytemplate-1.0.0.0+2512051103._debian-x86_64-12-x86_64.sha256 _SHA256)
  string(REGEX MATCH "^[^ ]*" checksum ${_SHA256} )
  configure_file(
    /home/deeprey/projects/deeprey-template/build/deepreytemplate-1.0-debian-x86_64-12.xml.in
    /home/deeprey/projects/deeprey-template/build/deepreytemplate-1.0-debian-x86_64-12.xml
    @ONLY
  )
