# Docker container as RPM build environment

Here you find a Docker file to create an environment containing all the dependencies you need.

1. Build it with `docker build -f contrib/docker/rpm/Dockerfile -t pgexporter-build .` (run this from the pgexporter root directory)
1. Run it with `docker run -it -v ./target:/root/rpmbuild/RPMS:Z pgexporter-build /bin/bash`
1. Run `~/build.sh` inside the container
1. The RPM will be placed in the `./target` directory (outside the container, from where you ran it)

**ATTENTION** You need to rebuild the Docker image for each build, since the repository contents are copied into it during build time!
