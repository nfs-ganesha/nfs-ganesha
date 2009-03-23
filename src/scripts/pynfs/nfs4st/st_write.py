from base_st_classes import *

## class WriteSuite(NFSSuite):
##     """Test operation 38: WRITE

##     FIXME: Complete. 
##     FIXME: Write to named attribute. 

##     Equivalence partitioning:

##     Input Condition: current filehandle
##         Valid equivalence classes:
##             file(10)
##             named attribute(11)
##         Invalid equivalence classes:
##             dir(12)
##             special device files(13)
##             invalid filehandle(14)
##     Input Condition: stateid
##         Valid equivalence classes:
##             all bits zero(20)
##             all bits one(21)
##             valid stateid from open(22)
##         Invalid equivalence classes:
##             invalid stateid(23)
##     Input Condition: offset
##         Valid equivalence classes:
##             zero(30)
##             nonzero(31)
##         Invalid equivalence classes:
##             -
##     Input Condition: stable
##         Valid equivalence classes:
##             UNSTABLE4(40)
##             DATA_SYNC4(41)
##             FILE_SYNC4(42)
##         Invalid equivalence classes:
##             invalid constant(43)
##     Input Condition: data
##         Valid equivalence classes:
##             no data(50)
##             some data(51)
##         Invalid equivalence classes:
##             -
##     """
##     #
##     # Testcases covering valid equivalence classes.
##     #
##     def testSimpleWrite(self):
##         """WRITE with stateid=zeros, no data and UNSTABLE4

##         Covered valid equivalence classes: 10, 20, 30, 40, 50
##         """
##         self.info_message("(TEST NOT IMPLEMENTED)")

##     def testStateidOne(self):
##         """WRITE with stateid=ones and DATA_SYNC4

##         Covered valid equivalence classes: 10, 21, 31, 41, 51
##         """
##         self.info_message("(TEST NOT IMPLEMENTED)")

##     def testWithOpen(self):
##         """WRITE with open and FILE_SYNC4

##         Covered valid equivalence classes: 10, 22, 30, 42, 51
##         """
##         self.info_message("(TEST NOT IMPLEMENTED)")
##     #
##     # Testcases covering invalid equivalence classes.
##     #
## ##     def testInvalid(self):
## ##         pass
