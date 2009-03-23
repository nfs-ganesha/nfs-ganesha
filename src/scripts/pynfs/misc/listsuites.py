#!/usr/bin/env python2

import sys
sys.path.append("..")

import nfs4st


def main():
    for attr in dir(nfs4st):
        if attr == "NFSSuite":
            continue
        if attr.endswith("Suite"):
            print attr


if __name__ == '__main__':
    main()



