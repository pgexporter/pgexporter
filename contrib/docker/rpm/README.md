# Docker container as RPM build environment

Here you find a Docker file to create an environment containing all the dependencies you need.

1. Build it with `docker build -t pgexporter-build .` (make sure `.` is the path to the directory containing this README.md, the Dockerfile and the build.sh file)
1. Run it with `docker run -it -v ./target:/root/rpmbuild/RPMS:Z pgexporter-build /bin/bash`
1. Run `~/build.sh` inside the container
1. The RPM will be placed in the `./target` directory (outside the container, from where you ran it)
