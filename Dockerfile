FROM ubuntu:trusty

MAINTAINER Guillaume Gimenez <ploki@blackmilk.fr>

RUN apt-get update

#Build deps
RUN apt-get -y install build-essential cmake libcurl4-gnutls-dev \
    	       	       libjansson-dev libkrb5-dev libjemalloc-dev \
		       libwbclient-dev bison flex doxygen libnfsidmap-dev \
		       uuid-dev libblkid-dev libcap-dev git libtirpc1
RUN apt-get -y install redis-server libhiredis-dev
#Run deps
RUN apt-get -y install rpcbind

USER root
COPY . /root/nfs-ganesha
RUN rm -rf /root/nfs-ganesha/build/ && mkdir /root/nfs-ganesha/build/
WORKDIR /root/nfs-ganesha/build

RUN cmake ../src/
RUN make -j 5
RUN make install
RUN ln -s /usr/bin/ganesha.nfsd /usr/bin/scality.nfsd

EXPOSE 564/tcp 2049/tcp 2049/tcp 875/tcp 875/udp 
VOLUME /logs
VOLUME /conf

CMD bash -c "service rpcbind start && service redis-server start && exec scality.nfsd -N INFO -f /conf/scality-nfsd.conf -F -L /logs/scality-nfsd.log"
