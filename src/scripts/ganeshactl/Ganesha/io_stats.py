from collections import namedtuple

# Basic I/O reply and io stats structs
BasicIO = namedtuple('BasicIO',
                     [requested,
                      transferred,
                      total_ops,
                      errors,
                      latency,
                      queue_wait])

IOReply = namedtuple('IOReply',
                     [status,
                      errormsg,
                      sampletime,
                      read,
                      write])

# pNFS layout stats reply and stats structs
Layout = namedtuple('Layout',
                    [total_ops,
                     errors,
                     delays])

pNFSReply = namedtuple('pNFSReply',
                       [status,
                        errormsg,
                        sampletime,
                        getdevinfo,
                        layout_get,
                        layout_commit,
                        layout_return,
                        layout_recall])

class IOstat(Object):
    def __init__(self, stat):
        pass
