


<!--
  - STOP

  - The NFS-Ganesha Project uses gerrithub to review patch submissions

  - See src/CONTRIBUTING_HOWTO.txt or [DevPolicy](https://github.com/nfs-ganesha/nfs-ganesha/wiki/DevPolicy)

  - In a nutshell, here's how to submit a patch for NFS-Ganesha to gerrithub

```
$ git clone ssh://USERNAMEHERE@review.gerrithub.io:29418/ffilz/nfs-ganesha
 $ cd nfs-ganesha
 nfs-ganesha$ git remote add gerrit ssh://USERNAMEHERE@review.gerrithub.io:29418/ffilz/nfs-ganesha
 nfs-ganesha$ git fetch gerrit
 nfs-ganesha$ ./src/scripts/git_hooks/install_git_hooks.sh
 nfs-ganesha$ git log gerrit/next..HEAD
 # this should ONLY list the commits you want to push!
 nfs-ganesha$ git push gerrit HEAD:refs/for/next
```

  - Look for your patch at [GerritHub](https://review.gerrithub.io/dashboard/self)

-->
