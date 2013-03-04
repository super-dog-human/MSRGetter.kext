#include <sys/systm.h>
#include <mach/mach_types.h>
#include <sys/types.h>
#include <sys/sysctl.h>



int pow(int base, int n) {
	
	int i, computation;
	computation = 1;
	for (i = 1; i <= n; i++) {
		computation = computation * base;
	}

	return computation;
}



int check_vendor(void) {

	int cpuid_raw1, cpuid_raw2, cpuid_raw3;
	__asm	{
//		xor     eax,eax
		mov eax, 0x0
		cpuid
		mov cpuid_raw3, ebx
		mov cpuid_raw2, edx
		mov cpuid_raw1, ecx
	}

	if ( cpuid_raw1 == 1818588270 && cpuid_raw2 == 1231384169 && cpuid_raw3 == 1970169159 ) {
		printf("CPU Vendor:GenuineIntel\n");
		return 0;
	} else {
		printf("Unsupported CPU Vendor\n");
		return 1;
	}

}



long check_model(void) {

	//	CPUID命令でモデルと拡張モデルの取得。
	int cpuid_raw;
	__asm	{
		mov eax, 0x1
		cpuid
		mov cpuid_raw, eax
	}

	//	モデルと拡張モデルを4桁ずつの数値に変換。
	int model = 0;
	int extmodel = 0;
	for (int i = 0; i <= 3 ; i++){
		model += (cpuid_raw>>(i+4) & 1) * pow(10, i);
		extmodel += (cpuid_raw>>(i+16) & 1) * pow(10, i);
	}
	printf("Model:%04d\n", model);
	printf("Extended Model:%04d\n", extmodel);

	long modelnumber = ((extmodel * 10000) + model);
	return modelnumber;

}



unsigned int read_msr_eax(int addr) {
	
	unsigned int msr_raw;
	__asm	{
		mov ecx, addr
		rdmsr
		mov msr_raw, eax
	}

	return msr_raw;

}



int get_core2_busclock(void) {

	// Core/Core2ならMSRからバスクロックを取得。
	unsigned int by_busclock = read_msr_eax(0xCD);
	by_busclock = (by_busclock>>2 & 1) * 100 + (by_busclock>>1 & 1) * 10 + (by_busclock & 1);

	// 2進数からバスクロックの割り当て。Intelのドキュメントによるとx66はx67。
	int busclock;
	switch (by_busclock) {
		case 101:	busclock = 100; break;
		case 1:		busclock = 133; break;
		case 11:	busclock = 167; break;
		case 10:	busclock = 200; break;
		case 0:		busclock = 267; break;
		case 100:	busclock = 333; break;
		case 110:	busclock = 400; break;
		default:	busclock = 0;
	}

	return busclock;

}



static int get_busclock SYSCTL_HANDLER_ARGS {

	long modelnumber = check_model();
	int model, extmodel;
	model = modelnumber % 10000;
	extmodel = modelnumber / 10000;

	// モデル・拡張モデルをもとにバスクロックを算出。
	int busclock;
	switch (extmodel) {

		case 0:			busclock = get_core2_busclock(); break;

		case 1:
		switch (model) {
			case 110:
			case 111:
			case 1100:	busclock = get_core2_busclock(); break;
			case 1010:
			case 1101:
			case 1110:	busclock = 133; break;
			default:	busclock = 0;
		}
		break;

		case 10:
		switch (model) {
			case 101:
			case 1100:
			case 1110:
			case 1111:	busclock = 133; break;	// Nehalem
			case 1010:
			case 1101:	busclock = 100; break;	// Sandy Bridge
			default:	busclock = 0;
		}
		break;

		case 11:
		switch (model) {
			case 110:	busclock = 133; break;	// Atom Cedarview
			case 1010:
			case 1100:	busclock = 100; break;
			default:	busclock = 0;
		}
		break;

		default: busclock = 0;

	}

	SYSCTL_OUT(req, &busclock, sizeof(uint32_t));

	return 0;

}



static int get_multiplier SYSCTL_HANDLER_ARGS {

	// MSRからIA32_PERF_STATUSを取得。
	unsigned int msr_status = read_msr_eax(0x198);

	//	MSRのコア倍率ビットを取りだす。8ビットから13ビットまでを10進数に戻して重みをかける。
	float multiplier = 0;
	for (int i = 0; i <= 5; i++) {
		multiplier += (msr_status>>(i+8) & 1) * pow(2, i);
	}

	//	コア倍率がn.5倍時の計算。恐らくCore/Core2のみ。
	if (msr_status>>14 & 1) {
		multiplier += 0.5;
	}	

	//	0はフラグ無し、1はバスクロック半減時、2はOC時。
	int energy_mode = 0;
	if (msr_status>>15 & 1) {
		energy_mode = 1;
//	} else if () {
//		energy_mode = 2;
	}

	// コンマ区切りでコア倍率と省エネフラグを返す。
	char out_multiplier[6];
	int radicand = 1;
	for (int i = 0; i <= 1; i++) {
		int tmp = multiplier / pow(10, radicand);
		out_multiplier[i] = tmp + '0';
		multiplier -= tmp * pow(10, radicand);
		radicand -= 1;
	}
	out_multiplier[2] = '.';
	out_multiplier[3] = (multiplier * 10) + '0';
	out_multiplier[4] = ',';
	out_multiplier[5] =  energy_mode + '0';	
	out_multiplier[6] = '\0';

	SYSCTL_OUT(req, &out_multiplier, sizeof(out_multiplier));

	return 0;

}


/*
static int get_coreid SYSCTL_HANDLER_ARGS {

	int apic_id = 0;

	__asm	{
		mov eax, 0x1
		mov ebx, 0x0
		mov ecx, 0x0
		mov edx, 0x0
		cpuid
		mov apic_id, ebx
	}

	apic_id = apic_id>>24;
	apic_id = apic_id && 0xFF;

	SYSCTL_OUT(req, &apic_id, sizeof(apic_id));

	return 0;

}
*/


/*
static int set_1a0 SYSCTL_HANDLER_ARGS {

	int addr = 0x1A0;
	unsigned int msr_eax = read_msr_eax(addr);
	unsigned int msr_edx = 0;

	__asm	{
		mov ecx, addr
		rdmsr
		mov msr_edx, edx
	}

	int eist_frag = msr_eax>>16 & 1;
	SYSCTL_OUT(req, &eist_frag, sizeof(eist_frag));

	//	右から17番目のビットを立てる。
//	msr_eax = msr_eax | 0x10000;
	msr_eax = msr_eax & 0xFFFEFFFF;

	eist_frag = msr_eax>>16 & 1;
	SYSCTL_OUT(req, &eist_frag, sizeof(eist_frag));

	__asm	{
		mov ecx, addr
		mov eax, msr_eax
		mov edx, msr_edx
		wrmsr
	}

	return 0;

}
*/



//	sysctlに新しいカテゴリmsrgetterを作成。
SYSCTL_DECL(_msrgetter);
SYSCTL_NODE(, OID_AUTO, msrgetter, CTLFLAG_RD, 0, "MSRGetter to Get Real CPU Clock.");

//	sysctlで引数が付いた物をそれぞれ実行。
SYSCTL_PROC		(_msrgetter,	OID_AUTO,	busclock,	CTLTYPE_INT|CTLFLAG_RD,		NULL,	0,	&get_busclock,		"I", "Return Bus Clock.");
SYSCTL_PROC		(_msrgetter,	OID_AUTO,	multiplier,	CTLTYPE_STRING|CTLFLAG_RD,	NULL,	0,	&get_multiplier,	"A", "Return Core Multiplier.");
//	SYSCTL_PROC		(_msrgetter,	OID_AUTO,	coreid,		CTLTYPE_INT|CTLFLAG_RD,		NULL,	0,	&get_coreid,	"I", "Return Current Core ID.");
//	SYSCTL_PROC		(_msrgetter,	OID_AUTO,	set1a0,		CTLTYPE_INT|CTLFLAG_RD,		NULL,	0,	&set_1a0,		"I", "Set Value to MSR 0x1A0.");


kern_return_t MSRGetter_start (kmod_info_t * ki, void * d) {
	
	if (check_vendor()) {
		return KERN_FAILURE;
	} else {
		check_model();
		sysctl_register_oid(&sysctl__msrgetter);
		sysctl_register_oid(&sysctl__msrgetter_busclock);
		sysctl_register_oid(&sysctl__msrgetter_multiplier);
//		sysctl_register_oid(&sysctl__msrgetter_coreid);
//		sysctl_register_oid(&sysctl__msrgetter_set1a0);
		printf("MSRGetter.kext Load Success.\n");
		return KERN_SUCCESS;
	}

}



kern_return_t MSRGetter_stop (kmod_info_t * ki, void * d) {
	sysctl_unregister_oid(&sysctl__msrgetter);
	sysctl_unregister_oid(&sysctl__msrgetter_busclock);
	sysctl_unregister_oid(&sysctl__msrgetter_multiplier);
//	sysctl_unregister_oid(&sysctl__msrgetter_coreid);
//	sysctl_unregister_oid(&sysctl__msrgetter_set1a0);
	printf("MSRGetter.kext Unload Success.\n");
	return KERN_SUCCESS;
}
