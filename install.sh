#!/bin/sh

VERSION="2.1.1"
wget "https://github.com/Youg-Otricked/qcm/releases/download/${VERSION}/qcm_${VERSION}_amd64.deb"
sudo apt install "./qcm_${VERSION}_amd64.deb"
