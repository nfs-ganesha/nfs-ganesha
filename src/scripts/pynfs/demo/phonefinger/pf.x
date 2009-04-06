
const PF_PORT = 20000;


struct PFargs {
    string user<8>;
};


struct PFresults {
    bool status;
    string phone<20>; 
};


program PF_PROGRAM {
	version PF_VERSION {
		void PFPROC_NULL(void) = 0;
		PFresults PFPROC_CALL(PFargs) = 1;
	} = 1;
} = 20000000;
