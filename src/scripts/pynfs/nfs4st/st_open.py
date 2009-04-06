from base_st_classes import *

## class OpenSuite(NFSSuite):
##     """Test operation 18: OPEN

##     FIXME: Verify that this eqv.part is correct, when updated state
##     description is available.

##     FIXME: Add test for file named "." and ".." in
##     open_claim_delegate_cur4.file, open_claim4.file and
##     open_claim4.file_delegate_prev. 

##     Equivalence partitioning:

##     Input Condition: seqid
##         Valid equivalence classes:
##             correct seqid(10)
##         Invalid equivalence classes:
##             to small seqid(11)
##             to large seqid(12)
##     Input Condition: share_access
##         Valid equivalence classes:
##             valid share_access(20)
##         Invalid equivalence classes:
##             invalid share_access(21)
##     Input Condition: share_deny
##         Valid equivalence classes:
##             valid share_deny(30)
##         Invalid equivalence classes:
##             invalid share_deny(31)
##     Input Condition: owner.clientid
##         Valid equivalence classes:
##             valid clientid(40)
##         Invalid equivalence classes:
##             stale clientid(41)
##     Input Condition: owner.opaque
##         Valid equivalence classes:
##             valid owner(50)
##         Invalid equivalence classes:
##             invalid owner(51)
##     Input Condition: openhow.opentype
##         Valid equivalence classes:
##             OPEN_NOCREATE4(60)
##             OPEN_CREATE4(61)
##         Invalid equivalence classes:
##             invalid openhow.opentype(62)
##     Input Condition: openhow.how
##         Valid equivalence classes:
##             UNCHECKED4(70)
##             GUARDED4(71)
##             EXCLUSIVE4(72)
##         Invalid equivalence classes:
##             invalid openhow.how(73)
##     Input Condition: openhow.how.createattrs
##         Valid equivalence classes:
##             valid createattrs(80)
##         Invalid equivalence classes:
##             invalid createattrs(80)
##     Input Condition: openhow.how.createverf
##         Valid equivalence classes:
##             matching verifier(90):
##             non-matching verifier(91)
##         Invalid equivalence classes:
##             -
##     Input Condition: claim.claim
##         Valid equivalence classes:
##             CLAIM_NULL(100)
##             CLAIM_PREVIOUS(101)
##             CLAIM_DELEGATE_CUR(102)
##             CLAIM_DELEGATE_PREV(103)
##         Invalid equivalence classes:
##             invalid claim.claim(104)
##     Input Condition: claim.file:
##         Valid equivalence classes:
##             valid filename(110)
##         Invalid equivalence classes:
##             non-utf8 filename(111)
##     Input Condition: claim.delegate_type
##         Valid equivalence classes:
##             valid claim.delegate_type(120)
##         Invalid equivalence classes:
##             invalid claim.delegate_type(121)
##     Input Condition: claim.delegate_cur_info.delegate_stateid
##         Valid equivalence classes:
##             valid stateid(130)
##         Invalid equivalence classes:
##             invalid stateid(131)
##     Input Condition: claim.delegate_cur_info.file
##         Valid equivalence classes:
##             valid filename(140)
##         Invalid equivalence classes:
##             invalid filenname(141)
##     Input Condition: claim.file_delegate_prev
##         Valid equivalence classes:
##             valid filename(150)
##         Invalid equivalence classes:
##             invalid filename(151)
##     """
##     # FIXME
##     pass
