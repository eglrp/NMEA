#pragma  once
#include "stdafx.h"//changed
#include "Position.h"
#include "solution.h"
#include "MStruct.h"
#include "ExtFun.h"
#include <iostream>
#include <fstream>
//using namespace math;

//#define  DMatrix				math::matrix<double>;
//typedef math::matrix<double> DMatrix;

static DdAmbInfo AmbInfoSys;
static DdObsInfo DdObsSys;
static DdCtrl DdControl;
static int epoch=0;

void EqualAmbInfo(DdAmbInfo ambinfo)
{
	AmbInfoSys=ambinfo;
}

void EqualObsInfo(DdObsInfo obsinfo)
{
	DdObsSys=obsinfo;
}

void EqualCtrl(DdCtrl dc)
{
	DdControl=dc;
}


/*NL ambiguity will be fixed in this part*/
extern  void deliverInsideAmbInfo(DdAmbInfo & ambinfo)
{
	ambinfo=AmbInfoSys;
}

/*
 *get next epoch data
 *I:
 *	Ofile
	line
	obsHeader
	epochdata2
*O:
*	epochdata1
*	nepoch
*	eventflag	10=end of file
*	simu			t1-t2
 */
void Position::NextEpoch(CStdioFile Ofile,CString line,ObsEpochData& epochData1,ObsHeader obsHeader,int nEpoch,int eventFlag,double simu,ObsEpochData epochData2,ProcessRinex readdata)
{
	while(simu<0.0)
	{
		readdata.GetEpochData(Ofile,line,epochData1,obsHeader,nEpoch,eventFlag);
		if (eventFlag==10)
		{
			//end of file
			Ofile.Close();
			break;
		}
		simu	=(eventFlag==0)?(epochData1.week-epochData2.week)*604800.0+epochData1.sec-epochData2.sec:simu;
	}
}

/*****************************the cycle slip detection*******************************************/
/*	detect cycle slip 
 *	I:
 *		pre			pre data
 *		cur			current data
 *		csthrs		threshold   (cycle)
 *return:
 *		1  cycle slip
 *		0  continuous
 *	Note: the detection is based on the zero difference obs, and the unit of phs is CYCLE
 */

 /*
  * detect cycle slip with geometry free
  * all units are CYCLE
  */
 double CycleSlipGF(double* predata,double* currdata,int index1,int index2,int sysid)
 {
	 double lam1=CLIGHT/FreqSys(sysid,index1),lam2=CLIGHT/FreqSys(sysid,index2);	
	 return -( predata[index1]-predata[index2]*lam2/lam1-(currdata[index1]-currdata[index2]*lam2/lam1) );	 
 }

 /*
  *here, the crds of both stations are known
  *phi-rho/lam=N1-N2+T.....
  *prephs    unit: cycle
  */
 double CycleSlipWL(double* prephs, double rhopre,double* currphs,double rhocur,int index1,int index2,int sysid)
 {
	 int coef[3];InitPtr(coef,3);
	 coef[index1]=1;coef[index2]=-1;
	 double wavelen=CLIGHT/CombFreq(sysid,coef);
	 double preWL=prephs[0]-prephs[1]-rhopre/wavelen;
	 double curWL=currphs[0]-currphs[1]-rhocur/wavelen;

	 return curWL-preWL;
 }

 void searchCycleSlip(double dataGF,double& N1, double& N2,int serachInterval,int index1,int index2,int sysid)
 {
	 double freq1=FreqSys(sysid,index1),freq2=FreqSys(sysid,index2);
	 double freqRatio=freq1/freq2;
	 double reGF=N1-freqRatio*N2;
	 double minelem=fabs(reGF-dataGF);
	 double reN1,reN2;
	 for (int i=-serachInterval;i<serachInterval+1;i++)
	 {
		 reN1=N1+(double)i;
		 for (int j=-serachInterval;j<serachInterval+1;j++)
		 {
			 reN2=N2+(double)j;
			 double gf=reN1-freqRatio*reN2;
			 if (fabs(reN1-freqRatio*reN2-dataGF)<=minelem)
			 {
				 minelem=fabs(reN1-freqRatio*reN2-dataGF);
				 N1=reN1;
				 N2=reN2;
			 }
		 }
	 }
 }

 double RepairCycleSlip(double dataGF,double dataWL,int index1,int index2,int sysid,double& N2)
 {
	 double freq1=FreqSys(sysid,index1),freq2=FreqSys(sysid,index2);
	 N2=freq2/(freq1-freq2)*(dataWL-dataGF);
	 double N1=dataGF+freq1/freq2*N2;
	 N1=ROUND(N1);
	 N2=ROUND(N2);
	 searchCycleSlip(dataGF,N1,N2,3,index1,index2,sysid);
	 return N1;
 }
 /* phs  cycle
  * cod  meter*/
 int CycleSlipMW(double* prephs, double* precod,double* currphs, double* currcod,int index1,int index2,int sysid)
 {
	 double mwCur=MWCom(currphs,currcod,index1,index2,sysid);
	 double mwPre=MWCom(prephs,precod,index1,index2,sysid);
	
	 return mwCur-mwPre;
 }

 /* Ncurr=Npre-dN */
 void RevisePreMw(AmbData& mw,double N1_N2)
 {
	 int index=mw.CurrentIndex;
	 for (int i=0;i<index-1;i++)
	 {
		 mw.Cycle[i]-=N1_N2;
	 }
 }


 void Position::CycleSlipDetectionBDSTriple(double thresGF,double thresMW,DdData& curData,DdData preData)
 {
	 CycleSlipDetection(thresGF,thresMW,0,1,curData,preData);
	 CycleSlipDetection(thresGF,thresMW,0,2,curData,preData);
 }
 /*
  *I:
  *	 phase unit: cycle
	 Note: the distances between the sate and rec are revised
  */

 /*
  *unit  cycle
  *
  **/
 void Position::GetMW(AmbData* mw,DdData curData)
 {
	 int prnlistMw[MAXNUMSATE];
	 InitPtr(prnlistMw,MAXNUMSATE);
	 for(int i=0;i<MAXNUMSATE;i++) prnlistMw[i]=mw[i].Prn;
	 int pos=-1;
	 for (int i=0;i<curData.pairNum;i++)
	 {
		 pos=FindPosInt(prnlistMw,MAXNUMSATE,curData.rovPrn[i]);
		 if (pos==-1)
		 {
			 for (int j=0;j<MAXNUMSATE;j++)
			 {
				 if(prnlistMw[j]<=0) pos=j;
				 prnlistMw[pos]=curData.rovPrn[i];
				 if(pos>=0) break;
			 }
		 }
		 if (curData.datarecord[i].numVadCod+curData.datarecord[i].numVadPhs>=4&&mw[pos].CurrentIndex<MAXOBSEPOCH-1)
		 {
			 mw[pos].Prn=curData.rovPrn[i];
			 mw[pos].CurrentIndex++;
			 mw[pos].Cycle[ mw[pos].CurrentIndex ]=MWCom(curData.datarecord[i].Phase,curData.datarecord[i].PsRange,0,1,Sysid(curData.rovPrn[i]));
			 mw[pos].VadFlag [ mw[pos].CurrentIndex ]=1;
			 mw[pos].LastObs=curData.sec;
			 if (mw[pos].CurrentIndex==0) mw[pos].FirstObs=curData.sec;
		 }
		 
	 }
 }
 
 
 /*triple freq are available
  *
  *	obs unit	cycle
  **/
 void Position::GetEWL(EwlData* ewl,DdData curData )
 {
	int sysid=Sysid(curData.refPrn);
	 
	 double P011=0.0,P110=0.0,Phi14_5=0.0,Phi0_11=0.0;
	int coef011[3],coef110[3],coef14_5[3],coef0_11[3];
	coef011[0]=0;	coef011[1]				=coef011[2]=1;
	coef0_11[0]=0;	coef0_11[1]=-1;		coef0_11[2]=1;
	coef110[0]			=coef110[1]=1;		coef110[2]=0;
	coef14_5[0]=1;	coef14_5[1]=4;		coef14_5[2]=-5;
	double lam14_5=CLIGHT/CombFreq(sysid,coef14_5);
	double lam0_11=CLIGHT/CombFreq(sysid,coef0_11);
	int prnlistEwl[MAXNUMSATE];
	InitPtr(prnlistEwl,MAXNUMSATE);
	for(int i=0;i<MAXNUMSATE;i++) prnlistEwl[i]=ewl[i].Prn;
	int pos=-1;
	for (int i=0;i<curData.pairNum;i++)
	{
		pos=FindPosInt(prnlistEwl,MAXNUMSATE,curData.rovPrn[i]);
		if (pos==-1)
		{
			for (int j=0;j<MAXNUMSATE;j++)
			{
				if(prnlistEwl[j]<=0) pos=j;
				prnlistEwl[pos]=curData.rovPrn[i];
				if(pos>=0) break;
			}
			
		}
		if (curData.datarecord[i].numVadCod+curData.datarecord[i].numVadPhs>=6&&ewl[pos].CurrentIndex<MAXOBSEPOCH-1)
		{
			ewl[pos].Prn=curData.rovPrn[i];
			ewl[pos].CurrentIndex++;
			Phi14_5=CombObsCycle(sysid,coef14_5,curData.datarecord[i].Phase);
			P110	=CombObs(sysid,coef110,curData.datarecord[i].PsRange);
			ewl[pos].EwlCycle[0][ ewl[pos].CurrentIndex ]=(P110-Phi14_5)/lam14_5;
			ewl[pos].VadFlag [0][ ewl[pos].CurrentIndex ]=1;
			
			Phi0_11=CombObsCycle(sysid,coef0_11,curData.datarecord[i].Phase);
			P011	=CombObs(sysid,coef011,curData.datarecord[i].PsRange);
			ewl[pos].EwlCycle[1][ ewl[pos].CurrentIndex ]=(P011-Phi0_11)/lam0_11;
			ewl[pos].VadFlag [1][ ewl[pos].CurrentIndex ]=1;
			ewl[pos].LastObs=curData.sec;
			if (ewl[pos].CurrentIndex==0) ewl[pos].FirstObs=curData.sec;
		}

	}

 }

 void Position::GetNL(fixinfo* infoWl,DdData Lcdata,AmbData* NL1)
 {
	 DdData LcdataCopy=Lcdata;
	 for(int j=0;j<LcdataCopy.pairNum;j++)
	 {
		 double rho=LcdataCopy.distRecSate_DD(j);
		 LcdataCopy.datarecord[j].Phase[0]-=rho;
		 LcdataCopy.datarecord[j].PsRange[0]-=rho;
	 }
	 
	 int prnlistNL[MAXNUMSATE],prnlistWL[MAXNUMSATE];
	 int sysid=Sysid(LcdataCopy.rovPrn[0]);
	 double f1=FreqSys(sysid,0),f2=FreqSys(sysid,1);
	 double lcd=LCD((int)(f1*1e-3),(int)(f2*1e-3) );
	 /*the coefficient of each freq */
	 double a=f1*1e-3/lcd,b=f2*1e-3/lcd;
	 double lam=CLIGHT/(a*f1-b*f2);
	for(int i=0;i<MAXNUMSATE;i++) 
	{
			 prnlistWL[i]=infoWl[i].prn;
			 prnlistNL[i]=LcdataCopy.rovPrn[i];
	 }
	 int  posNL=-1,posWL=-1;
	  for (int i=0;i<LcdataCopy.pairNum;i++)
	  {
		  posWL=FindPosInt(prnlistWL,MAXNUMSATE,LcdataCopy.rovPrn[i]);
		  posNL=FindPosInt(prnlistNL,MAXNUMSATE,LcdataCopy.rovPrn[i]);
		  if (posNL==-1)
		  {
			  for (int j=0;j<MAXNUMSATE;j++)
			  {
				  if(prnlistNL[j]<=0) 
				  {
					  posNL=j;
					  prnlistNL[j]=LcdataCopy.rovPrn[i];
					  break;
				  }
			  }
		  }
		  int epochNum=(int)(NL1[posNL].LastObs-NL1[posNL].FirstObs);
		  if (posWL>=0&&posNL>=0&&epochNum<MAXOBSEPOCH)
		  {
				  NL1[posNL].Prn=LcdataCopy.rovPrn[i];
				  NL1[posNL].CurrentIndex++;
				  double wl=0;
				  if(infoWl[posWL].fixepoch1>0)
				  {  
					  wl=infoWl[posWL].check1;
					  NL1[posNL].isWLInteger[NL1[posNL].CurrentIndex]=1;

					  /*modify the previous ambiguity*/
					  
					  for (int j=0;j<epochNum;j++)
					  {
						  if (NL1[posNL].isWLInteger[j]==0)
						  {
							  NL1[posNL].Cycle[j]-=(f2/(f1-f2)*wl);
							  NL1[posNL].isWLInteger[j]=1;
						  }
					  }
				  }
				  /*watch out the minus  "-"*/
				  NL1[posNL].Cycle[NL1[posNL].CurrentIndex]=-(LcdataCopy.datarecord[i].Phase[0]/(CLIGHT/(f1+f2))+f2/(f1-f2)*wl);
				  NL1[posNL].VadFlag[NL1[posNL].CurrentIndex]=1;
				  NL1[posNL].LastObs=LcdataCopy.sec;
				  if(NL1[posNL].CurrentIndex==0) 
				  {
					  NL1[posNL].FirstObs=LcdataCopy.sec;
				  }
		  }

	  }
 }

 void Position::outNL(AmbData* NL1,fixinfo* infoNL,fixinfo* infoWL,double thres_a)
 {
	 int prnlistNL[MAXNUMSATE];
	 InitPtr(prnlistNL,MAXNUMSATE);
	 for(int i=0;i<MAXNUMSATE;i++) 
	 {
		 //prnlistWL[i]=infoWL[i].prn;
		 prnlistNL[i]=NL1[i].Prn;
	 }
	 int posWL=-1,posNL=-1;
	 for (int i=0;i<MAXNUMSATE;i++)
	 {
		 posNL=FindPosInt(prnlistNL,MAXNUMSATE,infoWL[i].prn);
		 if (infoWL[i].fixepoch1>0&&posNL>-1&&infoNL[i].fixepoch1<=0)
		 {
			 if (NL1[posNL].BiasCycle()<thres_a)
			 {
				 infoNL[i].check1=NL1[posNL].CheckAmb();
				 infoNL[i].check2=infoNL[i].check1-infoWL[i].check1;
				 infoNL[i].fixepoch1=NL1[posNL].LastObs;
				 infoNL[i].fixepoch2=infoNL[i].fixepoch1;
			 }
		 }
	 }
 }
 void Position::outEwl(int nEpoch,DdData ts,EwlData* ewl,fstream& fout,fixinfo* info,
					double thres_a,int firstepoch,int epochend,DdCtrl ddctrl)
{
	GetEWL(ewl,ts);
	if (nEpoch>firstepoch)
	{
		cout<<ts.pairNum<<"  "<<endl;
		for (int i=0;i<MAXNUMSATE;i++)
		{
			int sys;
			if (ewl[i].Prn>0&&ewl[i].CurrentIndex>=0)
			{
				info[sys-1].valid=1;
				sys=(ddctrl.sysid==5)?ewl[i].Prn-200:ewl[i].Prn;
				if (ewl[i].BiasCycle(0)<thres_a && info[sys-1].fixepoch1==0)
				{
					info[sys-1].fixepoch1=nEpoch;
					info[sys-1].bias1=ewl[i].BiasCycle(0);
					info[sys-1].check1=ewl[i].EwlCheck(0);
				}
				if (ewl[i].BiasCycle(1)<thres_a&& info[sys-1].fixepoch2==0)
				{
					info[sys-1].fixepoch2=nEpoch;
					info[sys-1].bias2=ewl[i].BiasCycle(1);
					info[sys-1].check1=ewl[i].EwlCheck(2);
				}
				//cout<<setiosflags(ios::fixed)<<setprecision(1)<<setw(15)<<ewl[i].EwlCheck(0)+5.0*ewl[i].EwlCheck(1)<<endl;
				if (ewl[i].BiasCycle(0)>thres_a && nEpoch==epochend&&info[sys-1].fixepoch1==0)
				{
					info[sys-1].bias1=ewl[i].BiasCycle(0);
				}
				if (ewl[i].BiasCycle(1)>thres_a&& nEpoch==epochend&&info[sys-1].fixepoch2==0)
				{
					info[sys-1].bias2=ewl[i].BiasCycle(1);
				}
			}
		}

		 if (nEpoch==epochend)
		{
			fout<<DistofVector(ts.refRecPos,ts.rovRecPos,3)/1000<<endl;
			for (int k=0;k<32;k++)
			{
				if (info[k].valid>0)
				{
					fout<<setw(3)<<info[k].prn<<"  "<<setw(4)<<info[k].fixepoch1<<"  "<<
						setw(4)<<info[k].fixepoch2<<"  " <<setprecision(4)<<setw(7)<<info[k].bias1<<"  "
						<<setprecision(4)<<setw(7)<<info[k].bias2<<endl;
				}
			}
			return;
		}
 
 
	}
}

void  Position::outMw(int nEpoch,DdData ts,AmbData* mw,fstream& fout,fixinfo* info,
									double thres_a,int firstepoch,int epochend,DdCtrl ddctrl)
{
	GetMW(mw,ts);
	if (nEpoch>firstepoch)
	{
		cout<<ts.pairNum<<"  "<<endl;
		for (int i=0;i<MAXNUMSATE;i++)
		{
			int sys;
		
			if (mw[i].Prn>0&&mw[i].CurrentIndex>=0)
			{
				info[sys-1].valid=1;
				sys=(ddctrl.sysid==5)?mw[i].Prn-200:mw[i].Prn;
				if (mw[i].BiasCycle()<thres_a && info[sys-1].fixepoch1==0)
				{
					info[sys-1].fixepoch1=nEpoch;
					info[sys-1].bias1=mw[i].BiasCycle();
					info[sys-1].check1=mw[i].CheckAmb();
				}
			}
			//cout<<setiosflags(ios::fixed)<<setprecision(1)<<setw(15)<<ewl[i].EwlCheck(0)+5.0*ewl[i].EwlCheck(1)<<endl;
			if (mw[i].BiasCycle()>thres_a && nEpoch==epochend&&info[sys-1].fixepoch1==0)
			{
					info[sys-1].bias1=mw[i].BiasCycle();
			}
		}
	}

	if (nEpoch==epochend)//==epochend
	{
		//fout<<DistofVector(dddataCurr.refRecPos,dddataCurr.rovRecPos,3)/1000<<endl;
		for (int k=0;k<32;k++)
		{
			if (info[k].valid>0)
			{
				cout<<setw(3)<<info[k].prn<<"  "<<setw(4)<<info[k].fixepoch1<<"  "<<
					setw(4)<<info[k].fixepoch2<<"  " <<setprecision(4)<<setw(7)<<info[k].bias1<<"  "
					<<setprecision(4)<<setw(7)<<info[k].bias2<<setprecision(11)<<setw(12)<<info[k].check1<<"  "
					<<setprecision(11)<<setw(12)<<info[k].check2
					<<endl;
			}
		}
		epochend=epochend;
		return ;
	}
}



 /*
  *detect cycle slip double or triple frequency 
  *detect with GF and MW, L1/B1 is the reference obs
  *
  *
  *Note:
  *		if the obs of some frequencies are discontinued, the cycle slip happened
  *		for all double-frequency obs are available
  *		single frequency detection is not reliable
  *		extend the isSlip, for triple-frequency (to do)
  *		the unit of phase is cycle
  *		here, suppose thresMW >=0.5  cycle , thresGF>=0.3 cycle
 */
 void  Position:: CycleSlipDetection(double thresGF,double thresMW ,double thresWL, int index1,int index2,
									DdAmbInfo& ambinfoCur,DdData& curdata,DdData& predata,
									AmbData* mw,double& N1,double& N2)
 {
	 int i,j,k,GF=0,MW=0,WL=0,s=-1;
	 /*for multiple frequency, the index1 and index2 denote the frequencies uesd, [0,1,2]*/	
	int numCur=curdata.pairNum,numPre=predata.pairNum;
	 int prnlistCur[MAXNUMSATE],prnlistPre[MAXNUMSATE],prnlistMW[MAXNUMSATE];
	 InitPtr(prnlistCur,MAXNUMSATE);
	 InitPtr(prnlistPre,MAXNUMSATE);
	 InitPtr(prnlistMW,MAXNUMSATE);
	 /*Get list of prn */
	 for (i=0;i<numCur;i++) prnlistCur[i]=curdata.rovPrn[i];
	 for (j=0;j<numPre;j++)  prnlistPre[j]=predata.rovPrn[j];
	 for(i=0;i<MAXNUMSATE;i++) prnlistMW[i]=mw[i].Prn;
	 for (i=0;i<numCur;i++)
	 {
		 k=FindPosInt(prnlistPre,numPre,curdata.rovPrn[i]);
		 s=FindPosInt(prnlistMW,MAXNUMSATE,curdata.rovPrn[i]);
		 /* the double frequecncy obs(phs and psrange )  are required*/
		 int vadCur=curdata.datarecord[i].vadFlgPhs[index1]+curdata.datarecord[i].vadFlgPhs[index2];
		 /*Question: process the single frequency ?*/
		 if (k==-1)/*not found,  satellite  rises */
		 {
			 curdata.datarecord[i].isCycleSlip[index1]=0;
			 curdata.datarecord[i].isCycleSlip[index2]=0;
		 }                                                                                                                                                                                                                                                                                                                                                                                        
		 else if(predata.datarecord[k].vadFlgPhs[index1]+predata.datarecord[k].vadFlgPhs[index2]==2 && vadCur==2)
		 {
			 int sysid=Sysid(curdata.rovPrn[i]);
			 double deltaGF=CycleSlipGF(predata.datarecord[k].Phase,curdata.datarecord[i].Phase,index1,index2,sysid);
  			 double deltaMW=-CycleSlipMW(predata.datarecord[k].Phase,predata.datarecord[k].PsRange,curdata.datarecord[i].Phase,curdata.datarecord[i].PsRange,index1,index2,sysid);
			 
			 double rhoPre=predata.distRoverRover(k)-predata.distRoverRef()-predata.distBaseRover(k)+predata.distBaseRef();
			 double rhoCur=curdata.distRoverRover(i)-curdata.distRoverRef()-curdata.distBaseRover(i)+curdata.distBaseRef();
			 double deltaWL=CycleSlipWL(predata.datarecord[k].Phase,rhoPre,curdata.datarecord[i].Phase,rhoCur,index1,index2,sysid);
			 
			 WL=fabs(deltaWL)>thresWL?1:0;
			 GF=fabs(deltaGF)>thresGF?1:0;
			 MW=fabs(deltaMW)>thresMW?1:0;
			 if(GF+MW>0) 
			 {
				  cout<<curdata.rovPrn[i]<<"  "<<deltaGF<<"  "<<deltaWL<<endl;
				  cout<<CycleSlipMW(predata.datarecord[k].Phase,predata.datarecord[k].PsRange,curdata.datarecord[i].Phase,curdata.datarecord[i].PsRange,index1,index2,sysid)<<endl;
				  N1=RepairCycleSlip(deltaGF,deltaWL,index1,index2,sysid,N2);
				  predata.datarecord[k].Phase[0]+=N1;
				  predata.datarecord[k].Phase[1]+=N2;
				  RevisePreMw(mw[s],N1-N2);
				  //curdata.datarecord[i].isCycleSlip[index1]=1;
				  //curdata.datarecord[i].isCycleSlip[index2]=1;
			 }
		 }
	 }
 }

 void  Position:: CycleSlipDetection(double thresGF,double thresMW , int index1,int index2,DdData& curdata,DdData predata)
 {
	 int i,j,k,GF=0,MW=0,WL=0,s=-1;
	 /*for multiple frequency, the index1 and index2 denote the frequencies uesd, [0,1,2]*/	
	 int numCur=curdata.pairNum,numPre=predata.pairNum;
	 int prnlistCur[MAXNUMSATE],prnlistPre[MAXNUMSATE],prnlistMW[MAXNUMSATE];
	 InitPtr(prnlistCur,MAXNUMSATE);
	 InitPtr(prnlistPre,MAXNUMSATE);
	 InitPtr(prnlistMW,MAXNUMSATE);
	 /*Get list of prn */
	 for (i=0;i<numCur;i++) prnlistCur[i]=curdata.rovPrn[i];
	 for (j=0;j<numPre;j++)  prnlistPre[j]=predata.rovPrn[j];
	 for (i=0;i<numCur;i++)
	 {
		 k=FindPosInt(prnlistPre,numPre,curdata.rovPrn[i]);
		 /* the double frequecncy obs(phs and psrange )  are required*/
		 int vadCur=curdata.datarecord[i].vadFlgPhs[index1]+curdata.datarecord[i].vadFlgPhs[index2];
		 /*Question: process the single frequency ?*/
		 if (k==-1)/*not found,  satellite  rises */
		 {
			 curdata.datarecord[i].isCycleSlip[index1]=1;
			 curdata.datarecord[i].isCycleSlip[index2]=1;
		 }                                                                                                                                                                                                                                                                                                                                                                                        
		 else if(predata.datarecord[k].vadFlgPhs[index1]+predata.datarecord[k].vadFlgPhs[index2]==2 && vadCur==2)
		 {
			 int sysid=Sysid(curdata.rovPrn[i]);
			 double deltaGF=CycleSlipGF(predata.datarecord[k].Phase,curdata.datarecord[i].Phase,index1,index2,sysid);
			 double deltaMW=-CycleSlipMW(predata.datarecord[k].Phase,predata.datarecord[k].PsRange,curdata.datarecord[i].Phase,curdata.datarecord[i].PsRange,index1,index2,sysid);

			 GF=fabs(deltaGF)>thresGF?1:0;
			 MW=fabs(deltaMW)>thresMW?1:0;
			 if(GF+MW>0) 
			 {
				 curdata.datarecord[i].isCycleSlip[index1]=1;
				 curdata.datarecord[i].isCycleSlip[index2]=1;
			 }
		 }
	 }
 }

  /*******************************the end of cycle slip detection*****************************************/




 /*******************************the difference part*****************************************/
/*
	Select the sate on mask elevation 
	I:
		MaskEle		degree
	O:
		sppinfo	
		lastData		the selected obsdata
	
*/
bool Position::SelectSateOnEle(double MaskEle,SppInfo& sppinfo,ObsEpochData& lastData)
{
	ObsEpochData tempdata;
	tempdata=lastData;
	int count=0;
	for(int i=0;i<sppinfo.validnum;i++)
	{
		if(sppinfo.ele[i]>=MaskEle*D2R)
		{
			if (count<i)//count is less or equal to i
			{
				lastData.obsdatarecord[count]	=tempdata.obsdatarecord[i];
				sppinfo.satePos[count]				=sppinfo.satePos[i];
//				sppinfo.emiTime[count]			=sppinfo.emiTime[i];
				sppinfo.prnList[count]				=sppinfo.prnList[i];
				sppinfo.ele[count]						=sppinfo.ele[i];
				sppinfo.azi[count]						=sppinfo.azi[i];
				sppinfo.mapWet[count]			=sppinfo.mapWet[i];
				sppinfo.sateclkerr[count]			=sppinfo.sateclkerr[i];
				sppinfo.sateclkVel[count]			=sppinfo.sateclkVel[i];
				sppinfo.sateVel[count]				=sppinfo.sateVel[i];
				sppinfo.tropCorr[count]			=sppinfo.tropCorr[i];
				sppinfo.codeCorr[count]			=sppinfo.codeCorr[i];
				sppinfo.residual[count]				=sppinfo.residual[i];
			}
			count++;
		}
		
	}
	lastData.sateNum=count;
	sppinfo.validnum	=count;
	return sppinfo.validnum<4?false:true;
}

bool Position::SelectSateOnEleGlo(double MaskEle,SppInfoGlo& sppinfo,ObsEpochData& lastData)
{
	ObsEpochData tempdata;
	tempdata=lastData;
	int count=0;
	for(int i=0;i<sppinfo.validnum;i++)
	{
		if(sppinfo.ele[i]>=MaskEle*D2R)
		{
			if (count<i)//count is less or equal to i
			{
				lastData.obsdatarecord[count]	=tempdata.obsdatarecord[i];
				sppinfo.satePos[count]				=sppinfo.satePos[i];
				//				sppinfo.emiTime[count]			=sppinfo.emiTime[i];
				sppinfo.prnList[count]				=sppinfo.prnList[i];
				sppinfo.ele[count]						=sppinfo.ele[i];
				sppinfo.azi[count]						=sppinfo.azi[i];
				sppinfo.mapWet[count]			=sppinfo.mapWet[i];
				sppinfo.sateclkerr[count]			=sppinfo.sateclkerr[i];
				sppinfo.sateclkVel[count]			=sppinfo.sateclkVel[i];
				sppinfo.sateVel[count]				=sppinfo.sateVel[i];
				sppinfo.tropCorr[count]			=sppinfo.tropCorr[i];
				sppinfo.codeCorr[count]			=sppinfo.codeCorr[i];
				sppinfo.residual[count]				=sppinfo.residual[i];
				sppinfo.freqNum[count]			=sppinfo.freqNum[i];
			}
			count++;
		}

	}
	lastData.sateNum=count;
	sppinfo.validnum	=count;
	return sppinfo.validnum<4?false:true;
}

void Position::SetEleToObsinfo(DdObsInfo& obsinfo,SppInfo baseinfo,SppInfo roverinfo,int* pos1,int* pos2,int refprn,int count)
{
	int i,j,t=0;
	for (i=0;i<baseinfo.validnum;i++)
	{
		if (refprn==baseinfo.prnList[i])
		{
			obsinfo.eleRefBase=baseinfo.ele[i];
			j=i;
			break;
		}
	}
	for (i=0;i<count;i++)
	{
		if (i!=j)
		{
			obsinfo.eleRovBase[t]=baseinfo.ele[ pos1[i] ];
			t++;
		}
	}
	t=0;
	for (i=0;i<roverinfo.validnum;i++)
	{
		if (refprn==roverinfo.prnList[i])
		{
			obsinfo.eleRefRov=roverinfo.ele[i];
			j=i;
			break;
		}
	}
	for (i=0;i<count;i++)
	{
		if (i!=j)
		{
			obsinfo.eleRovRov[t]=roverinfo.ele[ pos2[i] ];
			t++;
		}
	}
	
}
void Position::SetEleToObsinfo(DdObsInfo& obsinfo,SppInfoGlo baseinfo,SppInfoGlo roverinfo,int* pos1,int* pos2,int refprn,int count)
{
	int i,j,t=0;
	for (i=0;i<baseinfo.validnum;i++)
	{
		if (refprn==baseinfo.prnList[i])
		{
			obsinfo.eleRefBase=baseinfo.ele[i];
			j=i;
			break;
		}
	}
	for (i=0;i<count;i++)
	{
		if (i!=j)
		{
			obsinfo.eleRovBase[t]=baseinfo.ele[ pos1[i] ];
			t++;
		}
	}
	t=0;
	for (i=0;i<roverinfo.validnum;i++)
	{
		if (refprn==roverinfo.prnList[i])
		{
			obsinfo.eleRefRov=roverinfo.ele[i];
			j=i;
			break;
		}
	}
	for (i=0;i<count;i++)
	{
		if (i!=j)
		{
			obsinfo.eleRovRov[t]=roverinfo.ele[ pos2[i] ];
			t++;
		}
	}

}


/*
	single difference between stations
	this function is a part of SelectRefSate, the sates' postion of two receivers had
	intersected previously. see
	I:
		pos1			intersect result, the position of sate in zdbase struct
		pos2			intersect result, the position of sate in zdrover struct
		count		the intersect number between two station
		refPrn		reference sate prn
		zdbase		data of base station
		zdrover		data of rover station
		baseinfo	the base station information of spp
		roverinfo	the rover station information of spp
	O:
		sddata		single difference data
	Note:
		not consider sateclk compensate (due to the difference of obs time)

	the raw data is calibrated by troposphere correction
	,the  receiver clock error(from SPP)
	and the receiving time
	t_obs-=clk_err	Li=Li-fi*clk_err  P=P-C*clk_err
*/
void Position::SDstation(int* pos1,int* pos2,int count,int refPrn,
					ObsEpochData zdbase,ObsEpochData zdrover,SppInfo baseinfo,SppInfo roverinfo,
					SdData& sddata)
{
	sddata.satnum=count;
	sddata.week	=zdbase.week;
	double dtrBase=baseinfo.dtr/CLIGHT;
	double dtrRover=roverinfo.dtr/CLIGHT;
	sddata.sec		=zdbase.sec-baseinfo.dtr/CLIGHT;
	for(int i=0;i<3;i++)
	{
		sddata.refRecPos[i]			=baseinfo.recPos[i];
		sddata.rovRecPos[i]			=roverinfo.recPos[i];
	}
	
	double freq, lamda;//////add the lamda
	for(int i=0;i<count;i++)
	{
		sddata.prn[i]						=baseinfo.prnList[pos1[i]];
		sddata.satePosBase[i]		=baseinfo.satePos[pos1[i]];
		sddata.satePosRov[i]			=roverinfo.satePos[pos2[i]];
		sddata.mapWet[i]				=(baseinfo.mapWet[pos1[i]]+roverinfo.mapWet[pos2[i]])/2.0;
		sddata.tropCor[i]				=roverinfo.tropCorr[pos2[i]]-baseinfo.tropCorr[pos1[i]];
		sddata.ele[i]				=(baseinfo.ele[pos1[i]]+roverinfo.ele[pos2[i]])/2.0;
		/* correct the obs with troposphere model( UNB3 )  */
		for(int j=0;j<NFREQ+NEXOBS;j++)
		{
			freq=Freq(zdbase.obsdatarecord[pos1[i]].PRN, j);//
			lamda=CLIGHT/freq;

			/*correct the sateclk, for asynchronous situation. if it is synchronous, sateclk_cort=0.0 */
			double sateclk_cort=CLIGHT*(roverinfo.sateclkerr[pos2[i]]-baseinfo.sateclkerr[pos1[i]]);
			if (zdrover.obsdatarecord[pos2[i]].vadFlgCod[j]==1&&zdbase.obsdatarecord[pos1[i]].vadFlgCod[j]==1)
			{
				sddata.sddatarecord[i].numVadCod++;
				sddata.sddatarecord[i].vadFlgCod[j]=1;
				sddata.sddatarecord[i].PsRange[j]		=zdrover.obsdatarecord[pos2[i]].PsRange[j]-roverinfo.tropCorr[pos2[i]]-
																		(zdbase.obsdatarecord[pos1[i]].PsRange[j]-baseinfo.tropCorr[pos1[i]])-
																		sateclk_cort;
			}
			if (zdrover.obsdatarecord[pos2[i]].vadFlgPhs[j]==1&&zdbase.obsdatarecord[pos1[i]].vadFlgPhs[j]==1)
			{
				sddata.sddatarecord[i].numVadPhs++;
				sddata.sddatarecord[i].vadFlgPhs[j]=1;
				sddata.sddatarecord[i].Phase[j]			=zdrover.obsdatarecord[pos2[i]].Phase[j]-roverinfo.tropCorr[pos2[i]]/lamda-
																		(zdbase.obsdatarecord[pos1[i]].Phase[j]-baseinfo.tropCorr[pos1[i]]/lamda)-
																		sateclk_cort/lamda;
			}
			
			
			
		}

	}
	//int sk=0;
}

/*single difference between stations of GLONASS  */
void Position::SDstation(int* pos1,int* pos2,int count,int refPrn,
					ObsEpochData zdbase,ObsEpochData zdrover,SppInfoGlo baseinfo,SppInfoGlo roverinfo,
					SdData& sddata)
{
	sddata.satnum=count;
	sddata.week	=zdbase.week;
	double dtrBase=baseinfo.dtr/CLIGHT;
	double dtrRover=roverinfo.dtr/CLIGHT;
	sddata.sec		=zdbase.sec-baseinfo.dtr/CLIGHT;
	for(int i=0;i<3;i++)
	{
		sddata.refRecPos[i]			=baseinfo.recPos[i];
		sddata.rovRecPos[i]			=roverinfo.recPos[i];
	}

	double freq, lamda;//////add the lamda
	for(int i=0;i<count;i++)
	{
		sddata.prn[i]						=baseinfo.prnList[pos1[i]];
		sddata.satePosBase[i]		=baseinfo.satePos[pos1[i]];
		sddata.satePosRov[i]			=roverinfo.satePos[pos2[i]];
		sddata.mapWet[i]				=(baseinfo.mapWet[pos1[i]]+roverinfo.mapWet[pos2[i]])/2.0;
		for(int j=0;j<NFREQ+NEXOBS;j++)
		{
			freq=Freq(zdbase.obsdatarecord[pos1[i]].PRN, j);//
			lamda=CLIGHT/freq;//////add the lamda//calibrate troposphere delay here
			if (zdrover.obsdatarecord[pos2[i]].vadFlgCod[j]==1&&zdbase.obsdatarecord[pos1[i]].vadFlgCod[j]==1)
			{
				sddata.sddatarecord[i].numVadCod++;
				sddata.sddatarecord[i].vadFlgCod[j]=1;
				sddata.sddatarecord[i].PsRange[j]		=zdrover.obsdatarecord[pos2[i]].PsRange[j]-roverinfo.tropCorr[pos2[i]]-
					(zdbase.obsdatarecord[pos1[i]].PsRange[j]-baseinfo.tropCorr[pos1[i]]);
			}
			if (zdrover.obsdatarecord[pos2[i]].vadFlgPhs[j]==1&&zdbase.obsdatarecord[pos1[i]].vadFlgPhs[j]==1)
			{
				sddata.sddatarecord[i].numVadPhs++;
				sddata.sddatarecord[i].vadFlgPhs[j]=1;
				sddata.sddatarecord[i].Phase[j]			=zdrover.obsdatarecord[pos2[i]].Phase[j]-roverinfo.tropCorr[pos2[i]]/lamda-
					(zdbase.obsdatarecord[pos1[i]].Phase[j]-baseinfo.tropCorr[pos1[i]]/lamda);
			}



		}

	}
	//int sk=0;
}

static int ReferSat=0;

/*return ref sate prn
 *
 * refPrnPre  Previous prn 
 */
int Position::SelectRefSate(SppInfo sppinfobase,SppInfo sppinforover,double maskEle,ObsEpochData lastDataBase,
	ObsEpochData lastDataRover,SdData& lastSdData,DdObsInfo& obsinfo,int is_initRTKDone,int& refPrnPre)
{
	int count=0;
	int num=(sppinfobase.validnum>=sppinforover.validnum) ? sppinfobase.validnum: sppinforover.validnum;
	int* prnlist1		=new int[num];
	int* pos1			=new int[num];
	int* pos2			=new int[num];
	double* ele		=new double[num];
	int ind=-1;
	/*intersect the PRN and record the index*/
	for(int i=0;i<sppinfobase.validnum;i++)
	{
		for(int j=0;j<sppinforover.validnum;j++)
		{
			if(sppinfobase.prnList[i]==sppinforover.prnList[j] && sppinfobase.ele[i]>=maskEle*D2R && sppinforover.ele[j]>=maskEle*D2R)
			{
				prnlist1[count]		=sppinfobase.prnList[i];
				pos1[count]			=i;
				pos2[count]			=j;
				ele[count]				=(sppinfobase.ele[i]+sppinforover.ele[j])/2;
				lastSdData.ele[count]=ele[count];
				/*set is_initRTKDone=1 and prnPreviuos=XXX, fix the refsat*/
				if (is_initRTKDone==1&&refPrnPre>0&&prnlist1[count]==refPrnPre)
					ind=count;
				
				count++;
			}
		}
	}
	if (count<4)	return 0;

	int temprefPrn;
	
	if((is_initRTKDone==1&&refPrnPre<=0) || (is_initRTKDone==0) )
		ind= AbsIndMaxInd(ele,count);//index of refsat

		temprefPrn=prnlist1[ind];

	// set the elevations to the obsinfo 
	SetEleToObsinfo(obsinfo,sppinfobase,sppinforover,pos1,pos2,temprefPrn,count);
//	refPrn=temprefPrn;
	
	SDstation(pos1,pos2,count,temprefPrn,lastDataBase,lastDataRover,sppinfobase,sppinforover,lastSdData);
	//----------------------------------------------
	
	delete[] pos1,pos2,ele,prnlist1;
	//DoubleDiff(refPrn,FindPosInt(lastSdData.prn,lastSdData.satnum,refPrn),lastSdData,dddataCurr);
	return temprefPrn;
	///int kkk=0;
	//return true;
}

int Position::SelectRefSate(SppInfoGlo sppinfobase,SppInfoGlo sppinforover,double maskEle,ObsEpochData lastDataBase,
									ObsEpochData lastDataRover,SdData& lastSdData,DdObsInfo& obsinfo,int* dNum)
{
		int count=0;
		int num=max(sppinfobase.validnum, sppinforover.validnum);
		int* prnlist1		=new int[num];
		int* pos1			=new int[num];
		int* pos2			=new int[num];
		double* ele		=new double[num];

		for(int i=0;i<sppinfobase.validnum;i++)
		{
			for(int j=0;j<sppinforover.validnum;j++)
			{
				if(sppinfobase.prnList[i]==sppinforover.prnList[j] && sppinfobase.ele[i]>=maskEle*D2R && sppinforover.ele[j]>=maskEle*D2R)
				{
					dNum[count]		=sppinfobase.freqNum[i];
					prnlist1[count]		=sppinfobase.prnList[i];
					pos1[count]			=i;
					pos2[count]			=j;
					ele[count]				=(sppinfobase.ele[i]+sppinforover.ele[j])/2;
					lastSdData.ele[count]=ele[count];
					count++;
				}
			}
		}
		if (count<4)	return 0;

		int ind;
		ind= AbsIndMaxInd(ele,count);//index of refsat
		int temprefPrn;
		temprefPrn=prnlist1[ind];
		// set the elevations to the obsinfo 
		SetEleToObsinfo(obsinfo,sppinfobase,sppinforover,pos1,pos2,temprefPrn,count);
		//	refPrn=temprefPrn;
		SDstation(pos1,pos2,count,temprefPrn,lastDataBase,lastDataRover,sppinfobase,sppinforover,lastSdData);
		//----------------------------------------------

		delete[] pos1,pos2,ele,prnlist1;
		//DoubleDiff(refPrn,FindPosInt(lastSdData.prn,lastSdData.satnum,refPrn),lastSdData,dddataCurr);
		return temprefPrn;
		///int kkk=0;
		//return true;


}
/*
	double difference 
	I:
		refPrn		reference sate prn  
		Ind			the index of refsate in sddata, 
		num			number of data in sddata
		sddata		the single difference data between stations, calibrate the troposphere and sateclk
	O:
		dddata		the difference
*/
void Position::DoubleDiff(int refPrn,int Ind,SdData sddata,DdData& dddata)
{
	int count=0;
	for(int i=0;i<3;i++)
	{
		dddata.refRecPos[i]				=sddata.refRecPos[i];
		dddata.rovRecPos[i]				=sddata.rovRecPos[i];
		dddata.refSatPos_Base[i]		=sddata.satePosBase[Ind].sateXYZ[i];
		dddata.refSatPos_Rov[i]			=sddata.satePosRov[Ind].sateXYZ[i];
	}

	for (int i=0;i<sddata.satnum;i++)
	{
		if(refPrn!=sddata.prn[i])
		{
			dddata.rovPrn[count]			=sddata.prn[i];
			dddata.satePosBase[count]	=sddata.satePosBase[i];
			dddata.satePosRov[count]		=sddata.satePosRov[i];
			dddata.mapWet[count]			=sddata.mapWet[i]-sddata.mapWet[Ind];
			dddata.tropCor[count]			=sddata.tropCor[i]-sddata.tropCor[Ind];
			dddata.ele[count]					=sddata.ele[i];
			for(int j=0;j<NFREQ+NEXOBS;j++)
			{
				if (sddata.sddatarecord[i].vadFlgCod[j]==1&&sddata.sddatarecord[Ind].vadFlgCod[j]==1)
				{
					dddata.datarecord[count].numVadCod++;
					dddata.datarecord[count].vadFlgCod[j]=1;
					dddata.datarecord[count].PsRange[j]		=sddata.sddatarecord[i].PsRange[j]-sddata.sddatarecord[Ind].PsRange[j];
				}
				if (sddata.sddatarecord[i].vadFlgPhs[j]==1&&sddata.sddatarecord[Ind].vadFlgPhs[j]==1)
				{
					dddata.datarecord[count].numVadPhs++;
					dddata.datarecord[count].vadFlgPhs[j]=1;
					dddata.datarecord[count].Phase[j]			=sddata.sddatarecord[i].Phase[j]-sddata.sddatarecord[Ind].Phase[j];
				}
				
			}
			count++;
		}
	}
	dddata.refPrn			=refPrn;
	dddata.week				=sddata.week;
	dddata.sec				=sddata.sec;
	dddata.pairNum		=sddata.satnum-1;
}

/*******************************the end of difference part*****************************************/

/******************************the process of DD data******************************************/

/*after checking data, combine the triple frequency data*/
void Position::ComObsPhs(DdCtrl& ddctrl,DdData& temp,DdData dddata)
{
	int i,j,numobs;
	numobs=ddctrl.PhsTypeNo();


	if (ddctrl.ddambctrl.flag<=3)
	{
		for (i=0;i<numobs;i++)
		{
			ddctrl.freqPhs[i]=FreqSys(ddctrl.sysid,ddctrl.ddambctrl.coef[0][i]-1);
		}
		for (j=0;j<numobs;j++)
		{
			for (i=0;i<dddata.pairNum;i++)
			{
				if (dddata.datarecord[i].vadFlgPhs[  ddctrl.ddambctrl.coef[0][j]-1  ]==1)
				{
					temp.datarecord[i].numVadPhs++;
					temp.datarecord[i].vadFlgPhs[j]=1;
					temp.datarecord[i].Phase[j]=dddata.datarecord[i].Phase[ ddctrl.ddambctrl.coef[0][j]-1 ];
					if(ddctrl.sysid!=2) temp.datarecord[i].Phase[j] *= (CLIGHT/ddctrl.freqPhs[j]);
				}
			}
		}
	}//end if

	if (ddctrl.ddambctrl.flag>3)
	{
		int coef[3],flag=0;
		double obs[3];

		for (j=0;j<numobs;j++)
		{
			for (int jj=0;jj<3;jj++)
			{
				coef[jj]=ddctrl.ddambctrl.coef[j][jj];
			}
			ddctrl.freqPhs[j]=CombFreq(ddctrl.sysid,coef);
			for (i=0;i<dddata.pairNum;i++)
			{
				flag=1; // 1 is ok
				for (int k=0;k<3;k++)
				{
					coef[k]=ddctrl.ddambctrl.coef[j][k];
					if (coef[k]!=0&&dddata.datarecord[i].vadFlgPhs[ k ]==1)
					{
						obs[k]=dddata.datarecord[i].Phase[k]*(CLIGHT/FreqSys(ddctrl.sysid,k));
					}
					else if (coef[k]!=0&&dddata.datarecord[i].vadFlgPhs[ k ]==0)
					{
						flag=0;
						break;
					}
					else if(coef[k]==0)
					{
						obs[k]=0.0;
					}
				}
				if (flag!=0)
				{
					temp.datarecord[i].numVadPhs++;
					temp.datarecord[i].vadFlgPhs[j]=1;
					temp.datarecord[i].Phase[j]=(ddctrl.sysid==2)?dddata.datarecord[i].Phase[j]:CombObs(ddctrl.sysid,coef,obs);
				}

			}
		}
	}
}


/*if not checked*/
void Position::ComObsCod(DdCtrl& ddctrl,DdData& temp,DdData dddata,DdObsInfo& ddobsinfo)
{
	int i,j,numobs;
	temp.pairNum=dddata.pairNum;
	temp.refPrn=dddata.refPrn;
	temp.week=dddata.week;
	temp.sec=dddata.sec;
	
	for (i=0;i<3;i++)
	{
		temp.refRecPos[i]=dddata.refRecPos[i];
		temp.rovRecPos[i]=dddata.rovRecPos[i];
		temp.refSatPos_Rov[i]=dddata.refSatPos_Rov[i];
		temp.refSatPos_Base[i]=dddata.refSatPos_Base[i];
	}
	for (i=0;i<dddata.pairNum;i++)
	{
		temp.rovPrn[i]=dddata.rovPrn[i];
		temp.satePosBase[i]=dddata.satePosBase[i];
		temp.satePosRov[i]=dddata.satePosRov[i];
		temp.mapWet[i]=dddata.mapWet[i];
		temp.ele[i]=dddata.ele[i];
	}

	/*check the valid data*/
	numobs=(ddctrl.pseudoFlag>3)?ddctrl.pseudoFlag-3:ddctrl.pseudoFlag;
	if (ddctrl.pseudoFlag<=3)
	{
		for (i=0;i<numobs;i++)
		{
			ddctrl.freqCod[i]=FreqSys(ddctrl.sysid,ddctrl.pseudoCoef[0][i]-1);
		}
		for (j=0;j<numobs;j++)
		{
			for (i=0;i<dddata.pairNum;i++)
			{
				if (dddata.datarecord[i].vadFlgCod[  ddctrl.pseudoCoef[0][j]-1  ]==1)
				{
					ddobsinfo.prnlistCod[j][ ddobsinfo.numCod[j] ]=dddata.rovPrn[i];
					ddobsinfo.numCod[j]++;
					temp.datarecord[i].numVadCod++;
					temp.datarecord[i].vadFlgCod[j]=1;
					temp.datarecord[i].PsRange[j]=dddata.datarecord[i].PsRange[ ddctrl.pseudoCoef[0][j]-1 ];
				}
			}
		}
	}//end if

	if (ddctrl.pseudoFlag>3)
	{
		int coef[3],flag=0;
		double obs[3];

		for (j=0;j<numobs;j++)
		{
			for (int jj=0;jj<3;jj++)
			{
				coef[jj]=ddctrl.pseudoCoef[j][jj];
			}
			ddctrl.freqCod[j]=CombFreq(ddctrl.sysid,coef);

			for (i=0;i<dddata.pairNum;i++)
			{
				flag=1; // 1 is ok
				for (int k=0;k<3;k++)
				{
					coef[k]=ddctrl.pseudoCoef[j][k];
					if (coef[k]!=0&&dddata.datarecord[i].vadFlgCod[ k ]==1)
					{
						obs[k]=dddata.datarecord[i].PsRange[k];
					}
					else if (coef[k]!=0&&dddata.datarecord[i].vadFlgCod[ k ]==0)
					{
						flag=0;
						break;
					}
					else if(coef[k]==0)
					{
						obs[k]=0.0;
					}
				}
				if (flag!=0)
				{
					ddobsinfo.prnlistCod[j][ ddobsinfo.numCod[j] ]=dddata.rovPrn[i];
					ddobsinfo.numCod[j]++;
		
					temp.datarecord[i].numVadCod++;
					temp.datarecord[i].vadFlgCod[j]=1;
					temp.datarecord[i].PsRange[j]=CombObs(ddctrl.sysid,coef,obs);
				}
				
			}
		}
	}
	
}
void Position::ComObsPhs(DdCtrl& ddctrl,DdData& temp,DdData dddata,DdObsInfo& ddobsinfo,DdAmbInfo& curambinfo)
{
	int i,j,numobs;
	curambinfo.pairNum=dddata.pairNum;
	numobs=ddctrl.PhsTypeNo();
	curambinfo.freqNum=numobs;


	if (ddctrl.ddambctrl.flag<=3)
	{
		for (i=0;i<numobs;i++)
		{
			ddctrl.freqPhs[i]=FreqSys(ddctrl.sysid,ddctrl.ddambctrl.coef[0][i]-1);
		}
		for (j=0;j<numobs;j++)
		{
			for (i=0;i<dddata.pairNum;i++)
			{
				if (dddata.datarecord[i].vadFlgPhs[  ddctrl.ddambctrl.coef[0][j]-1  ]==1)
				{
					ddobsinfo.prnlistPhs[j][ ddobsinfo.numPhs[j] ]=dddata.rovPrn[i];
					curambinfo.prnList[j][ ddobsinfo.numPhs[j] ]=dddata.rovPrn[i];
					ddobsinfo.numPhs[j]++;
					temp.datarecord[i].numVadPhs++;
					temp.datarecord[i].vadFlgPhs[j]=1;
					temp.datarecord[i].Phase[j]=dddata.datarecord[i].Phase[ ddctrl.ddambctrl.coef[0][j]-1 ];
					if(ddctrl.sysid!=2) temp.datarecord[i].Phase[j] *= (CLIGHT/ddctrl.freqPhs[j]);
				}
			}
		}
	}//end if

	if (ddctrl.ddambctrl.flag>3)
	{
		int coef[3],flag=0;
		double obs[3];

		for (j=0;j<numobs;j++)
		{
			for (int jj=0;jj<3;jj++)
			{
				coef[jj]=ddctrl.ddambctrl.coef[j][jj];
			}
			ddctrl.freqPhs[j]=CombFreq(ddctrl.sysid,coef);
			for (i=0;i<dddata.pairNum;i++)
			{
				flag=1; // 1 is ok
				for (int k=0;k<3;k++)
				{
					coef[k]=ddctrl.ddambctrl.coef[j][k];
					if (coef[k]!=0&&dddata.datarecord[i].vadFlgPhs[ k ]==1)
					{
						obs[k]=dddata.datarecord[i].Phase[k]*(CLIGHT/FreqSys(ddctrl.sysid,k));
					}
					else if (coef[k]!=0&&dddata.datarecord[i].vadFlgPhs[ k ]==0)
					{
						flag=0;
						break;
					}
					else if(coef[k]==0)
					{
						obs[k]=0.0;
					}
				}
				if (flag!=0)
				{
					ddobsinfo.prnlistPhs[j][ ddobsinfo.numPhs[j] ]=dddata.rovPrn[i];
					curambinfo.prnList[j][ ddobsinfo.numPhs[j] ]=dddata.rovPrn[i];
					ddobsinfo.numPhs[j]++;
					temp.datarecord[i].numVadPhs++;
					temp.datarecord[i].vadFlgPhs[j]=1;
					temp.datarecord[i].Phase[j]=(ddctrl.sysid==2)?dddata.datarecord[i].Phase[j]:CombObs(ddctrl.sysid,coef,obs);
				}

			}
		}
	}
	PtrEqual(ddctrl.freqPhs,curambinfo.freq,NFREQ); 
}

void Position::ComObsPhsGlo(DdCtrl& ddctrl,DdData& temp,DdData dddata,DdObsInfo& ddobsinfo,DdAmbInfo& curambinfo)
{
	int i,j,numobs;
	curambinfo.pairNum=dddata.pairNum;
	numobs=ddctrl.PhsTypeNo();
	curambinfo.freqNum=numobs;

		for (i=0;i<numobs;i++)
		{
			ddctrl.freqPhs[i]=FreqSys(ddctrl.sysid,ddctrl.ddambctrl.coef[0][i]-1);
		}
		for (j=0;j<numobs;j++)
		{
			for (i=0;i<dddata.pairNum;i++)
			{
				if (dddata.datarecord[i].vadFlgPhs[  ddctrl.ddambctrl.coef[0][j]-1  ]==1)
				{
					ddobsinfo.prnlistPhs[j][ ddobsinfo.numPhs[j] ]=dddata.rovPrn[i];
					curambinfo.prnList[j][ ddobsinfo.numPhs[j] ]=dddata.rovPrn[i];
					ddobsinfo.numPhs[j]++;
					temp.datarecord[i].numVadPhs++;
					temp.datarecord[i].vadFlgPhs[j]=1;
					temp.datarecord[i].Phase[j]=dddata.datarecord[i].Phase[ ddctrl.ddambctrl.coef[0][j]-1 ];
				}
			}
		}

}


/*
 *combine the observation, according to the DdCtrl
 *simultaneously, set the current DdObsInfo
 *and set the current DdAmbInfo
 */
DdData Position::ComObsPhsCod(DdCtrl& ddctrl,DdObsInfo& ddobsinfo,DdAmbInfo& curambinfo,DdData dddata)
{
	DdData temp;
	ComObsCod(ddctrl,temp,dddata,ddobsinfo);
	ComObsPhs(ddctrl,temp,dddata,ddobsinfo,curambinfo);
	curambinfo.refSate=dddata.refPrn;
	return temp;
}

DdData Position::ComObsPhsCodGlo(DdCtrl& ddctrl,DdObsInfo& ddobsinfo,DdAmbInfo& curambinfo,DdData dddata,SppInfoGlo sppinfo)
{
	DdData temp;
	ComObsCod(ddctrl,temp,dddata,ddobsinfo);
	ComObsPhsGlo(ddctrl,temp,dddata,ddobsinfo,curambinfo);
	return temp;
}

/****************************the end of the process of DD data********************************************/

/*
 *after combing the observation, pass the preamb to curamb
 */
 void Position:: PassPreAmb(DdAmbInfo preamb,DdAmbInfo& curamb,DdCtrl ddctrl)
{
	int num=ddctrl.PhsTypeNo();
	int i,j,k;
	for (i=0;i<num;i++)
	{
		for (j=0;j<preamb.NoSat(i);j++)
		{
			for (k=0;k<curamb.NoSat(i);k++)
			{
				if (preamb.prnList[i][j]==curamb.prnList[i][k])
				{
					curamb.fixFlag[i][k]=preamb.fixFlag[i][k];
					curamb.fixSolu[i][k]=preamb.fixSolu[i][k];
					break;
				}
			}
		}
	}
}

/**************************Design     Matrix**********************************************/

 /*
 part of FormDdErrEq
 Form the design matrix for position and constance L
	I:
		dddata
	O:
		DesMatPos
		l				the range difference pairNum*1
	Note: 
		Obs 
		P^{Rov}_{R} - P^{Ref}_{R}-(	P^{Rov}_{B} - P^{Ref}_{B}	)
*/
void Position::FormDesMatPos(math::matrix<double>& DesMatPos,math::matrix<double>& L,DdData dddata,DdObsInfo ddobsinfo,DdCtrl ddctrl)
{
	double dxyzRefRov[3];//dxyz of refsat to rover station
	double dxyzRefBase[3];//dxyz of refsat to base station
	double dxyzRovRov[3];
	double dxyzRovBase[3];
	int i,j;
	for( i=0;i<3;i++)
	{ 
		dxyzRefRov[i]		=dddata.rovRecPos[i]-dddata.refSatPos_Rov[i];//the unknown
		dxyzRefBase[i]		=dddata.refRecPos[i]-dddata.refSatPos_Base[i];
	}
	double  distRefRov	=Norm(dxyzRefRov,3)+geodistcorr(dddata.refSatPos_Rov,dddata.rovRecPos);//range refsat rover station
	double	distRefBase	=Norm(dxyzRefBase,3)+geodistcorr(dddata.refSatPos_Base,dddata.refRecPos);//range between refsate and base station

	math::matrix<double>temp(dddata.pairNum,3);
	math::matrix<double>tempL(dddata.pairNum,1);
	double  distRovBase,distRovRov;
	for( i=0;i<dddata.pairNum;i++)
	{
		for( j=0;j<3;j++)
		{
			dxyzRovRov[j]		=dddata.rovRecPos[j]-dddata.satePosRov[i].sateXYZ[j];//the unknown
			dxyzRovBase[j]		=dddata.refRecPos[j]-dddata.satePosBase[i].sateXYZ[j];
		}
		distRovRov	=Norm(dxyzRovRov,3)+geodistcorr(dddata.satePosRov[i],dddata.rovRecPos);
		distRovBase	=Norm(dxyzRovBase,3)+geodistcorr(dddata.satePosBase[i],dddata.refRecPos);
		for( j=0;j<3;j++)
		{
			temp(i,j)	=dxyzRovRov[j]/distRovRov-dxyzRefRov[j]/distRefRov;
		}
		tempL(i,0)=distRovRov-distRefRov-(distRovBase-distRefBase);
	}

	int 	num=ddctrl.CodTypeNo();
	int cnt=0,k=0;
	for (i=0;i<num;i++)
	{
		for (j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgCod[i]==1)
			{
				L(cnt,0)=tempL(j,0);
				for (k=0;k<3;k++)
				{
					DesMatPos(cnt,k)=temp(j,k);
				}
				cnt++;
			}
		}
	}

	num=ddctrl.PhsTypeNo();
	for (i=0;i<num;i++)
	{
		for (j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgPhs[i]==1)
			{
				L(cnt,0)=tempL(j,0);
				for (k=0;k<3;k++)
				{
					DesMatPos(cnt,k)=temp(j,k);
				}
				cnt++;
			}
		}
	}
}

void Position::FormDesMatPos(math::matrix<double>& DesMatPos,math::matrix<double>& L,DdData dddata,int CodTypeNo,int PhsTypeNo)
{
	double dxyzRefRov[3];//dxyz of refsat to rover station
	double dxyzRefBase[3];//dxyz of refsat to base station
	double dxyzRovRov[3];
	double dxyzRovBase[3];
	int i,j;
	for( i=0;i<3;i++)
	{ 
		dxyzRefRov[i]		=dddata.rovRecPos[i]-dddata.refSatPos_Rov[i];//the unknown
		dxyzRefBase[i]		=dddata.refRecPos[i]-dddata.refSatPos_Base[i];
	}
	double  distRefRov	=Norm(dxyzRefRov,3)+geodistcorr(dddata.refSatPos_Rov,dddata.rovRecPos);//range refsat rover station
	double	distRefBase	=Norm(dxyzRefBase,3)+geodistcorr(dddata.refSatPos_Base,dddata.refRecPos);//range between refsate and base station

	math::matrix<double>temp(dddata.pairNum,3);
	math::matrix<double>tempL(dddata.pairNum,1);
	double  distRovBase,distRovRov;
	for( i=0;i<dddata.pairNum;i++)
	{
		for( j=0;j<3;j++)
		{
			dxyzRovRov[j]		=dddata.rovRecPos[j]-dddata.satePosRov[i].sateXYZ[j];//the unknown
			dxyzRovBase[j]		=dddata.refRecPos[j]-dddata.satePosBase[i].sateXYZ[j];
		}
		distRovRov	=Norm(dxyzRovRov,3)+geodistcorr(dddata.satePosRov[i],dddata.rovRecPos);
		distRovBase	=Norm(dxyzRovBase,3)+geodistcorr(dddata.satePosBase[i],dddata.refRecPos);
		for( j=0;j<3;j++)
		{
			temp(i,j)	=dxyzRovRov[j]/distRovRov-dxyzRefRov[j]/distRefRov;
		}
		tempL(i,0)=distRovRov-distRefRov-(distRovBase-distRefBase);
	}

	int 	num=CodTypeNo;
	int cnt=0,k=0;
	for (i=0;i<num;i++)
	{
		for (j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgCod[i]==1)
			{
				L(cnt,0)=tempL(j,0);
				for (k=0;k<3;k++)
				{
					DesMatPos(cnt,k)=temp(j,k);
				}
				cnt++;
			}
		}
	}

	num=PhsTypeNo;
	for (i=0;i<num;i++)
	{
		for (j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgPhs[i]==1)
			{
				L(cnt,0)=tempL(j,0);
				for (k=0;k<3;k++)
				{
					DesMatPos(cnt,k)=temp(j,k);
				}
				cnt++;
			}
		}
	}
}


/*
	part of FormDdErrEq
	form design matrix for tropsphere(corrected by UNB3)
	I:
		ddctrl
	O:
		DesMatTrop
		
*/
void Position::FormDesMatTrop(math::matrix<double>& DesMatTrop,DdCtrl ddctrl,DdData dddata)
{
	if(ddctrl.tropFlag==1)
	{
		int num=ddctrl.CodTypeNo();
		int cnt=0;
		for (int i=0;i<num;i++)
		{
			for (int j=0;j<dddata.pairNum;j++)
			{
				if (dddata.datarecord[j].vadFlgCod[i]==1)
				{
					DesMatTrop(cnt,0)=dddata.mapWet[j];
					cnt++;
				}
			}
		}
		num=ddctrl.PhsTypeNo();
		for (int i=0;i<num;i++)
		{
			for (int j=0;j<dddata.pairNum;j++)
			{
				if (dddata.datarecord[j].vadFlgPhs[i]==1)
				{
					DesMatTrop(cnt,0)=dddata.mapWet[j];
					cnt++;
				}
			}
		}
	}
}

void Position::FormDesMatTrop_Interval(DdCtrl ddctrl,math::matrix<double>& DesMatTrop_Cur,
															double* ptrMapPre,double* ptrMapCur,DdData dddata_cur,DdData dddata_pre)
{
	int num=ddctrl.CodTypeNo();
	int i,j,cnt=0;
	for (i=0;i<num;i++)
	{
		for (j=0;j<dddata_cur.pairNum;j++)
		{
			if (dddata_cur.datarecord[j].vadFlgCod[i]==1)
			{
				int index=FindPosInt(dddata_pre.rovPrn,dddata_pre.pairNum,dddata_cur.rovPrn[j]);
				DesMatTrop_Cur(cnt++,0)=(index==-1)?ptrMapCur[j]:ptrMapPre[index];
			}
		}
	}

	num=ddctrl.PhsTypeNo();
	for (i=0;i<num;i++)
	{
		for (j=0;j<dddata_cur.pairNum;j++)
		{
			if (dddata_cur.datarecord[j].vadFlgPhs[i]==1)
			{
				int index=FindPosInt(dddata_pre.rovPrn,dddata_pre.pairNum,dddata_cur.rovPrn[j]);
				DesMatTrop_Cur(cnt++,0)=(index==-1)?ptrMapCur[j]:ptrMapPre[index];
			}
		}
	}
}

/*reform the desmat of trop, with the certain Interval  */
void Position::FormDesMatTrop_ReNew(DdCtrl ddctrl, double nEpoch,double& firstTime,double& curTime, ObsEpochData roverData,
															DdData dddataCur,DdData dddataPre,double* ptrMapCur,double* ptrMapPre,
															math::matrix<double> DesMatTrop, math::matrix<double>& DesMatTrop_Hour,double interval)
{
	if (nEpoch==1)
	{
		firstTime=roverData.sec;
		DesMatTrop_Hour=DesMatTrop;
		InitPtr(ptrMapCur,MAXNUMSATE);
		PtrEqual(dddataCur.mapWet,ptrMapCur,dddataCur.pairNum);
	}
	else
	{
		curTime=roverData.sec;
		if (curTime-firstTime>=interval)
		{
			firstTime=roverData.sec;
			DesMatTrop_Hour=DesMatTrop;
			InitPtr(ptrMapCur,MAXNUMSATE);
			PtrEqual(dddataCur.mapWet,ptrMapCur,dddataCur.pairNum);
		}
		else
		{
			FormDesMatTrop_Interval(ddctrl,DesMatTrop_Hour,ptrMapPre,ptrMapCur,dddataCur,dddataPre);
			InitPtr(ptrMapCur,MAXNUMSATE);
			PtrEqual(dddataCur.mapWet,ptrMapCur,dddataCur.pairNum);
		}
	}
	InitPtr(ptrMapPre,MAXNUMSATE);
	PtrEqual(ptrMapCur,ptrMapPre,dddataCur.pairNum);
}


/*
	DesMatIono	=[	phase; pseudo];phaseMatIono=[L1 L2 L5]
	all data needed are available by default
*/
void Position::FormDesMatIono(math::matrix<double>& DesMatIono,DdCtrl ddctrl,DdData dddata)
{
	if (ddctrl.ionoFlag!=1 && ddctrl.ionoFlag!=3)
	{
		int num=ddctrl.CodTypeNo();
		int cnt=0;int cnt1=0;
		double freq1=FreqSys(ddctrl.sysid,0);
		for (int i=0;i<num;i++)
		{
			cnt1=0;
			for (int j=0;j<dddata.pairNum;j++)
			{
				if (dddata.datarecord[j].vadFlgCod[i]==1)
				{
					DesMatIono(cnt,cnt1)=SQ(freq1/ddctrl.freqCod[i]);
					cnt++;cnt1++;
				}
				else if (dddata.datarecord[j].vadFlgCod[i]==0)
				{
					cnt1++;
				}
			}
		}
		num=ddctrl.PhsTypeNo();
		for (int i=0;i<num;i++)
		{
			cnt1=0;
			for (int j=0;j<dddata.pairNum;j++)
			{
				if (dddata.datarecord[j].vadFlgPhs[i]==1)
				{
					DesMatIono(cnt,cnt1)=-SQ(freq1/ddctrl.freqPhs[i]);
					cnt++;
					cnt1++;
				}
				else if (dddata.datarecord[j].vadFlgCod[i]==0)
				{
					cnt1++;
				}

			}
		}
	}
}

void Position::FormDesMatIono(math::matrix<double>& DesMatIono,DdData dddata,int numCodType,int numPhsType,double* freqCod,double* freqPhs)
{
	int num=numCodType;
	int cnt=0;int cnt1=0;
	double freq1=FreqSys(Sysid(dddata.refPrn),0);
	for (int i=0;i<num;i++)
	{
		cnt1=0;
		for (int j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgCod[i]==1)
			{
				DesMatIono(cnt,cnt1)=SQ(freq1/freqCod[i]);
				cnt++;cnt1++;
			}
			else if (dddata.datarecord[j].vadFlgCod[i]==0)
			{
				cnt1++;
			}
		}
	}
	num=numPhsType;
	for (int i=0;i<num;i++)
	{
		cnt1=0;
		for (int j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgPhs[i]==1)
			{
				DesMatIono(cnt,cnt1)=-SQ(freq1/freqPhs[i]);
				cnt++;
				cnt1++;
			}
			else if (dddata.datarecord[j].vadFlgCod[i]==0)
			{
				cnt1++;
			}

		}
	}

}

void Position::FormDesMatIonoBDSErtk(math::matrix<double>& DesMatIono,DdData dddata,int numCodType,int numPhsType,double* freqCod,double* freqPhs)
{
	int num=numCodType;
	int cnt=0;int cnt1=0;
	double freq1=FreqSys(Sysid(dddata.refPrn),0);
	for (int i=0;i<num;i++)
	{
		cnt1=0;
		for (int j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgCod[i]==1)
			{
				DesMatIono(cnt,cnt1)=SQ(freq1/freqCod[i]);
				cnt++;cnt1++;
			}
			else if (dddata.datarecord[j].vadFlgCod[i]==0)
			{
				cnt1++;
			}
		}
	}
	num=numPhsType;
	for (int i=0;i<num;i++)
	{
		cnt1=0;
		for (int j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgPhs[i]==1)
			{
				if(i==0) DesMatIono(cnt,cnt1)=-(freq1+4*SQ(freq1)/FREQ7_BDS-5*SQ(freq1)/FREQ6_BDS)/freqPhs[i];
				if(i==1) DesMatIono(cnt,cnt1)=-(-SQ(freq1)/FREQ7_BDS+SQ(freq1)/FREQ6_BDS)/freqPhs[i];
				if(i==2) DesMatIono(cnt,cnt1)=-1.0;
				cnt++;
				cnt1++;
			}
			else if (dddata.datarecord[j].vadFlgCod[i]==0)
			{
				cnt1++;
			}

		}
	}

}

void Position::FormDesMatIonoGlo(math::matrix<double>& DesMatIono,DdCtrl ddctrl,DdData dddata,double* dNum)
{
	if (ddctrl.ionoFlag!=1 && ddctrl.ionoFlag!=3)
	{
		int num=ddctrl.CodTypeNo();
		int cnt=0;int cnt1=0;
		double freq1=FreqSys(ddctrl.sysid,0);
		for (int i=0;i<num;i++)
		{
			cnt1=0;
			for (int j=0;j<dddata.pairNum;j++)
			{
				if (dddata.datarecord[j].vadFlgCod[i]==1)
				{
					DesMatIono(cnt,cnt1)=SQ(FREQ1_GLORATIO)/SQ(FreqSysGlo(i,dNum[j])*1e-9);//SQ(freq1/ddctrl.freqCod[i]);
					cnt++;
					cnt1++;
				}
				else if (dddata.datarecord[j].vadFlgCod[i]==0)
				{
					cnt1++;
				}
			}
		}
		num=ddctrl.PhsTypeNo();
		for (int i=0;i<num;i++)
		{
			cnt1=0;
			for (int j=0;j<dddata.pairNum;j++)
			{
				if (dddata.datarecord[j].vadFlgPhs[i]==1)
				{
					DesMatIono(cnt,cnt1)=-SQ(FREQ1_GLORATIO)/SQ(FreqSysGlo(i,dNum[j])*1e-9);//SQ(freq1/ddctrl.freqPhs[i]);
					cnt++;
					cnt1++;
				}
				else if (dddata.datarecord[j].vadFlgCod[i]==0)
				{
					cnt1++;
				}

			}
		}
	}
}

/*
	form design matrix of ambiguity
	I:
		ddctrl  reserved
	O:
		DesMatAmb 
		0= combination 1=single freq	2=double	3=triple
*/
void Position::FormDesMatAmb(math::matrix<double>& DesMatAmb,DdCtrl ddctrl,DdData dddata,DdObsInfo ddobsinfo,DdAmbInfo ambinfo)
{
		int num;//=(ddctrl.pseudoFlag>3)?ddctrl.pseudoFlag-3:ddctrl.pseudoFlag;
		int cnt=0,i,j,k;
		double lam=0.0;
		cnt=ddobsinfo.SumCod();

		num=ddctrl.PhsTypeNo();
		int cnt1=0;
		for (i=0;i<num;i++)
		{
			lam=CLIGHT/ddctrl.freqPhs[i];
			for (j=0;j<dddata.pairNum;j++)
			{
				if (dddata.datarecord[j].vadFlgPhs[i]==1)
				{
					int pos,num=ambinfo.NoSat(i);
					pos=FindPosInt(ambinfo.prnList[i],num,dddata.rovPrn[j]);
					if (pos==-1)
					{
						DesMatAmb(cnt,cnt1++)=-lam;
					}
					else
					{
						if(ambinfo.fixFlag[i][pos]!=1) DesMatAmb(cnt,cnt1++)=-lam;
					}
					cnt++;
				}
				
			}
		}
}

int Position::FormDesMatAmb(math::matrix<double>& DesMatAmb,DdData dddata,int numPhsType,int numCod,double*freqPhs,DdAmbInfo ambinfo)
{
	int col=ambinfo.TotalUnfixNum(numPhsType);
	if(col==0) return 0;
	
	//DMatrix tDesAmb(numPhs+numCod,col);
	int numTypePhs;//=(ddctrl.pseudoFlag>3)?ddctrl.pseudoFlag-3:ddctrl.pseudoFlag;
	int cnt=0,i,j,k;
	double lam=0.0;
	cnt=numCod;

	numTypePhs=numPhsType;
	int cnt1=0;
	for (i=0;i<numTypePhs;i++)
	{
		lam=CLIGHT/freqPhs[i];
		for (j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgPhs[i]==1)
			{
				int pos,num=ambinfo.NoSat(i);
				pos=FindPosInt(ambinfo.prnList[i],num,dddata.rovPrn[j]);
				if (pos==-1)
				{
					DesMatAmb(cnt,cnt1++)=-lam;
				}
				else
				{
					if(ambinfo.fixFlag[i][pos]!=1) DesMatAmb(cnt,cnt1++)=-lam;
				}
				cnt++;
			}

		}
	}

	return 1;
}
void Position:: FormDesMatAmbGlo(math::matrix<double>& DesMatAmb,DdCtrl ddctrl,DdData dddata)
{
	int num;//=(ddctrl.pseudoFlag>3)?ddctrl.pseudoFlag-3:ddctrl.pseudoFlag;
	int cnt=0,i,j,k;
	cnt=dddata.SumCod();

	num=ddctrl.PhsTypeNo();
	int cnt1=0;
	for (i=0;i<num;i++)
	{
		//lam=CLIGHT/ddctrl.freqPhs[i];
		for (j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgPhs[i]==1)
			{
				DesMatAmb(cnt++,cnt1++)=-1;
			}
		}
	}
}

/*
	Form residual Prs
	I:
		l				the range difference of DD     dddata.pairNum*1
		dddata		
	O:
		L				the residual of DD, ddobs-ddrange       (phsFlag*pairNum + pseudoFlag*pairnum)*1
*/
void Position::FormResidual(math::matrix<double>& L,DdData dddata,int numTypeCod,int numTypePhs)
{
	int num=numTypeCod;
	int cnt=0;
	for (int i=0;i<num;i++)
	{
		for (int j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgCod[i]==1)
			{
				L(cnt,0)=dddata.datarecord[j].PsRange[i]-L(cnt,0);
				cnt++;
			}
		}
	}
	num=numTypePhs;
	for (int i=0;i<num;i++)
	{
		for (int j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgPhs[i]==1)
			{
				L(cnt,0)=dddata.datarecord[j].Phase[i]-L(cnt,0);
				cnt++;
			}
		}
	}
}
void Position::FormResidual(math::matrix<double>& L,DdData dddata,DdCtrl ddctrl,DdAmbInfo ambinfo)
{
	int num=ddctrl.CodTypeNo();
	int cnt=0;
	for (int i=0;i<num;i++)
	{
		for (int j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgCod[i]==1)
			{
				L(cnt,0)=dddata.datarecord[j].PsRange[i]-L(cnt,0);
				cnt++;
			}
		}
	}
	num=ddctrl.PhsTypeNo();
	for (int i=0;i<num;i++)
	{
		for (int j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgPhs[i]==1)
			{
				L(cnt,0)=dddata.datarecord[j].Phase[i]-L(cnt,0);
				cnt++;
			}
		}
	}
}
void Position::FormResidual(math::matrix<double>& L,math::matrix<double>& V,DdData dddata,DdCtrl ddctrl,DdAmbInfo ambinfo)
{
	int num=ddctrl.CodTypeNo();
	int cnt=0;
	for (int i=0;i<num;i++)
	{
		for (int j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgCod[i]==1)
			{
				L(cnt,0)=dddata.datarecord[j].PsRange[i]-L(cnt,0);
				V(cnt,0)=dddata.datarecord[j].PsRange[i];
				cnt++;
			}
		}
	}
	num=ddctrl.PhsTypeNo();
	for (int i=0;i<num;i++)
	{
		for (int j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgPhs[i]==1)
			{
				L(cnt,0)=dddata.datarecord[j].Phase[i]-L(cnt,0);
				cnt++;
			}
		}
	}
}

/*
	reform the error equation of dd
	change the ambiguity design matrix and constant vector(residual) 
	I:
		ddambinfo	preambinfo
		ddctrl
		curdddata		current dddata
	O:
		DesMatAmb
		L
*/

void Position::ReFormConstWithAmb(math::matrix<double>& L, DdAmbInfo ambinfo,int numCod,int TypePhsNo,double* freqPhs,DdData dddata)
{
	int num	=TypePhsNo;
	int i,j,k,cnt1=0;
	int cnt=numCod;
	double lam;
	for (i=0;i<num;i++)
	{
		lam=CLIGHT/freqPhs[i];
		for (j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgPhs[i]==1)
			{
				int pos,numsate=ambinfo.NoSat(i);
				pos=FindPosInt(ambinfo.prnList[i],numsate,dddata.rovPrn[j]);
				if (pos!=-1)
				{
					if(ambinfo.fixFlag[i][pos]==1) 
						L(cnt,0)+=lam*ambinfo.fixSolu[i][pos];
				}
				cnt++;
			}
		}
	}
}

void Position::ReFormConstWithAmb(math::matrix<double>& L, DdAmbInfo ambinfo,DdObsInfo ddobsinfo,DdCtrl ddctrl,DdData dddata)
{
	int num	=ddctrl.PhsTypeNo();
	int i,j,k,cnt1=0;
	int cnt=ddobsinfo.SumCod();
	double lam;
	for (i=0;i<num;i++)
	{
		lam=CLIGHT/ddctrl.freqPhs[i];
		for (j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgPhs[i]==1)
			{
				int pos,numsate=ambinfo.NoSat(i);
				pos=FindPosInt(ambinfo.prnList[i],numsate,dddata.rovPrn[j]);
				if (pos!=-1)
				{
					if(ambinfo.fixFlag[i][pos]==1) 
						L(cnt,0)+=lam*ambinfo.fixSolu[i][pos];
				}
				cnt++;
			}
		}
	}
}
/*
 * form weight, of uncombined obs
 */
math::matrix<double> Position::FormWeight(DdCtrl ddctrl,DdData curdata,DdObsInfo obsinfo)
{
	int numPhs=obsinfo.SumPhs(),numCod=obsinfo.SumCod();
	int phsType=ddctrl.PhsTypeNo(),codType=ddctrl.CodTypeNo();
	//for sd
	double corefSat=cofactor(obsinfo.eleRefBase,ddctrl.weightMode)+cofactor(obsinfo.eleRefRov,ddctrl.weightMode);
	math::matrix<double> cofact(curdata.pairNum,curdata.pairNum);
	double* cofactptr=new double[curdata.pairNum];
	int i,j,k;
	for (i=0;i<curdata.pairNum;i++)
	{
		//for sd 
		cofact(i,i)=cofactor(obsinfo.eleRovBase[i],ddctrl.weightMode)+cofactor(obsinfo.eleRovRov[i],ddctrl.weightMode)
						+corefSat;
		cofactptr[i]=cofact(i,i);
	}
	//for code
	math::matrix<double>temp(numCod,numCod);
	int cnt=0;int numCodi;
	if (ddctrl.pseudoFlag<=3 )
	{
		for (i=0;i<codType;i++)
		{
			numCodi=obsinfo.NoCod(i);
			if ( curdata.pairNum==numCodi)
			{
				SetPtrToMatdiag(cofactptr,curdata.pairNum,temp,cnt+1);
				cnt+=curdata.pairNum;
			}
			else
			{
				for (j=0;j<curdata.pairNum;j++)
				{
					for (k=0;k<numCodi;k++)
					{
						if (curdata.rovPrn[j]==obsinfo.prnlistCod[i][k])
						{
							temp(cnt,cnt)=cofactptr[j];
							cnt++;
							break;
						}
					}
				}
			}
		}
	}
	else if (ddctrl.pseudoFlag>3)
	{
		math::matrix<double>C=EyeMat(codType);
		temp=Kronecker(C,cofact,2);
		int count=0;
		
		for (k=0;k<codType;k++)
		{
			count=0;
			for (i=0;i<k;i++) count+=obsinfo.numCod[i];
			for (i=0;i<curdata.pairNum;i++)
			{
				if (curdata.datarecord[i].vadFlgCod[k]!=0)
				{
					count++;
				}
				else 
				{
					temp=RemoveRowCol(temp,count+1);
				}
			}
			
		}
	}
	temp=temp/SQ(ddctrl.sigmaPhs*10.0);
	//phs
	math::matrix<double>temp1(numPhs,numPhs);
	cnt=0;int numphsi;
	if (ddctrl.ddambctrl.flag<=3 )
	{
		for (i=0;i<phsType;i++)
		{
			numphsi=obsinfo.NoPhs(i);
			if ( curdata.pairNum==numphsi)
			{
				SetPtrToMatdiag(cofactptr,curdata.pairNum,temp1,cnt+1);
				cnt+=curdata.pairNum;
			}
			else
			{
				for (j=0;j<curdata.pairNum;j++)
				{
					for (k=0;k<numphsi;k++)
					{
						if (curdata.rovPrn[j]==obsinfo.prnlistPhs[i][k])
						{
							temp1(cnt,cnt)=cofactptr[j];
							cnt++;
							break;
						}
					}
				}
			}
		}
	}
	else if (ddctrl.ddambctrl.flag>3)
	{
		math::matrix<double>C=EyeMat(phsType);
		temp1=Kronecker(C,cofact,2);
		int count=0;

		for (k=0;k<phsType;k++)
		{
			count=0;
			for (i=0;i<k;i++) count+=obsinfo.numPhs[i];
			for (i=0;i<curdata.pairNum;i++)
			{
				if (curdata.datarecord[i].vadFlgPhs[k]!=0)
				{
					count++;
				}
				else 
				{
					temp1=RemoveRowCol(temp1,count+1);
				}
			}

		}
	}
	temp1=temp1/SQ(ddctrl.sigmaCod*10);
	delete[] cofactptr;
	return CholeskyInv(DiagMatSym(temp,temp1));
};


math::matrix<double> Position::FormWeightVc(DdCtrl ddctrl,DdData curdata,DdObsInfo obsinfo)
{
	math::matrix<double>weight;
	int i,j,codType=ddctrl.CodTypeNo(),phsType=ddctrl.PhsTypeNo();
	math::matrix<double> w;
	for (i=0;i<codType;i++)
	{
		int num=obsinfo.NoCod(i);
		math::matrix<double>D(num,num+1);
		w=ZeroMat(num+1,num+1);
		w(0,0)=cofactor(obsinfo.eleRefBase,ddctrl.weightMode)+cofactor(obsinfo.eleRefRov,ddctrl.weightMode);
		w(0,0)*=100.0;
		for (j=0;j<num;j++) D(j,0)=-1.0;
		int index=1;
		for (j=1;j<curdata.pairNum+1;j++)	
		{
			if (curdata.datarecord[j-1].vadFlgCod[i]==1)
			{
				w(index,index)=cofactor(obsinfo.eleRovBase[j-1],ddctrl.weightMode)+cofactor(obsinfo.eleRovRov[j-1],ddctrl.weightMode);
				w(index,index)*=100.0;
				D(index-1,index)=1.0;
				index++;
			}
		}
		w=D*w*(~D);

		w=CholeskyInv(w);
		weight=i>0?DiagMatSym(weight,w):w;
	}

	for (i=0;i<phsType;i++)
	{
		int num=obsinfo.NoPhs(i);
		math::matrix<double>D(num,num+1);
		w=ZeroMat(num+1,num+1);
		w(0,0)=cofactor(obsinfo.eleRefBase,ddctrl.weightMode)+cofactor(obsinfo.eleRefRov,ddctrl.weightMode);
		w(0,0)/=100.0;
		for (j=0;j<num;j++) D(j,0)=-1.0;
		int index=1;
		for (j=1;j<curdata.pairNum+1;j++)	
		{
			if (curdata.datarecord[j-1].vadFlgPhs[i]==1)
			{
				w(index,index)=cofactor(obsinfo.eleRovBase[j-1],ddctrl.weightMode)+cofactor(obsinfo.eleRovRov[j-1],ddctrl.weightMode);
				w(index,index)/=100.0;
				D(index-1,index)=1.0;
				index++;
			}
		}
		w=D*w*(~D);
		w=CholeskyInv(w);
		weight=DiagMatSym(weight,w);
	}
	return weight;
}

DMatrix Position::FormWeightSingleFreqPhs(int singleFreqObsNum,double eleRefBase,double eleRefRov,double* eleRovRov,double* eleRovBase)
{
	DMatrix Q(singleFreqObsNum,singleFreqObsNum);
	for (int i=0;i<singleFreqObsNum;i++)
	{
		for(int j=i;j<singleFreqObsNum;j++)
		{
			Q(i,j)=cofactor(eleRefBase,1)+cofactor(eleRefRov,1);
			Q(j,i)=Q(i,j);
		}
	}

	for (int i=0;i<singleFreqObsNum;i++) Q(i,i)+=(cofactor(eleRovRov[i],1)+cofactor(eleRovBase[i],1));
	return CholeskyInv(Q);
}
/*
 *form weight of ERTK 1.(1,4,-5) and 2.(0,-1,1)
 *Input:
 *		numSingle   single frequency obs of phase
 */
DMatrix Position::FormWeightErtk(int singleFreqObsNum,double eleRefBase,double eleRefRov,double* eleRovRov,double* eleRovBase)
{
	int coef1[3]={1,4,-5},coef2[3]={0,-1,1};
	DMatrix initMat(2,2),Qinit(singleFreqObsNum,singleFreqObsNum),P;
	double lam1=CLIGHT/CombFreq(5,coef1),lam2=CLIGHT/CombFreq(5,coef2),lam[3]={CLIGHT/FreqSys(5,0),CLIGHT/FreqSys(5,1),CLIGHT/FreqSys(5,2)};
	initMat(0,0)=SQ(lam1)*(SQ(coef1[0]/lam[0])+SQ(coef1[1]/lam[1])+SQ(coef1[2]/lam[2]));
	initMat(1,1)=SQ(lam2)*(SQ(coef2[0]/lam[0])+SQ(coef2[1]/lam[1])+SQ(coef2[2]/lam[2]));
	initMat(0,1)=initMat(1,0)=lam2*lam1*(coef1[0]*coef2[0]/SQ(lam[0])+coef1[1]*coef2[1]/SQ(lam[1])+coef1[2]*coef2[2]/SQ(lam[2]));
	for (int i=0;i<singleFreqObsNum;i++)
	{
		for(int j=i;j<singleFreqObsNum;j++)
		{
			Qinit(i,j)=cofactor(eleRefBase,1)+cofactor(eleRefRov,1);
			Qinit(j,i)=Qinit(i,j);
		}
	}
	for (int i=0;i<singleFreqObsNum;i++)
	{
		Qinit(i,i)+=cofactor(eleRovRov[i],1)+cofactor(eleRovBase[i],1);
	}
	return Kronecker(CholeskyInv(initMat),CholeskyInv(Qinit),4);
}



DMatrix Position::FormWeightErtkNl(int numSinlge,double eleRefBase,double eleRefRov,double* eleRovRov,double* eleRovBase)
{
	int coef1[3]={1,4,-5},coef2[3]={0,-1,1};
	DMatrix initMat(3,3),Qinit(numSinlge,numSinlge),P;
	double lam1=CLIGHT/CombFreq(5,coef1),lam2=CLIGHT/CombFreq(5,coef2),lam[3]={CLIGHT/FreqSys(5,0),CLIGHT/FreqSys(5,1),CLIGHT/FreqSys(5,2)};
	initMat(0,0)=SQ(lam1)*(SQ(coef1[0]/lam[0])+SQ(coef1[1]/lam[1])+SQ(coef1[2]/lam[2]));
	initMat(1,1)=SQ(lam2)*(SQ(coef2[0]/lam[0])+SQ(coef2[1]/lam[1])+SQ(coef2[2]/lam[2]));
	initMat(0,1)=initMat(1,0)=lam2*lam1*(coef1[0]*coef2[0]/SQ(lam[0])+coef1[1]*coef2[1]/SQ(lam[1])+coef1[2]*coef2[2]/SQ(lam[2]));
	initMat(2,0)=initMat(0,2)=lam1/lam[0];
	initMat(2,2)=1.0;
	for (int i=0;i<numSinlge;i++)
	{
		for(int j=i;j<numSinlge;j++)
		{
			Qinit(i,j)=cofactor(eleRefBase,1)+cofactor(eleRefRov,1);
			Qinit(j,i)=Qinit(i,j);
		}
	}
	for (int i=0;i<numSinlge;i++)
	{
		Qinit(i,i)+=cofactor(eleRovRov[i],1)+cofactor(eleRovBase[i],1);
	}
	return Kronecker(CholeskyInv(initMat),CholeskyInv(Qinit),4);
}
/*
	Form error equation of dd
	I:
		dddata
	O:
		DesMatPos		n*3
		DesMatTrop		n*?
		DesMatIono		n*n or n*0

		Weight				to be optimized
		L						n*1
	Note:
		DesMat	=[	phase; pseudo];	phaseMat=[L1 L2 L5]
		the parameter list is [x, y, z, trop, iono, amb, others]'
		observation model:	Ddobs	=Ddrho+DdTrop+DdIono-Lambda*DdAmb+���� 
		always	(rover-reference) of roverstation-(rover-reference) of basestation
		use the partition matrix operation,here
*/
void Position::FormDdErrEq(math::matrix<double>& DesMatPos,math::matrix<double>& DesMatTrop,math::matrix<double>& DesMatIono,math::matrix<double>& DesMatAmb,
							math::matrix<double>& Weight, math::matrix<double>& L,DdData dddata,DdCtrl ddctrl,DdAmbInfo ambinfo,DdObsInfo ddobsinfo)
{
	// form the design matrix for position	 and the constance L
	//ddobsinfo.SumCod()+ddobsinfo.SumPhs()
	
	FormDesMatPos(DesMatPos, L ,dddata,ddobsinfo,ddctrl);

	FormDesMatAmb(DesMatAmb, ddctrl,dddata,ddobsinfo,ambinfo);

	FormDesMatIono(DesMatIono,ddctrl,dddata);
	
	FormDesMatTrop( DesMatTrop, ddctrl,dddata);
	
	FormResidual(L,dddata,ddctrl,ambinfo);

	ReFormConstWithAmb(L,ambinfo,ddobsinfo,ddctrl,dddata);
	
	/* use FormWeightVc*/
	//Weight=FormWeight(ddctrl,dddata,ddobsinfo);
}

void Position::FormDdErrEq(math::matrix<double>& DesMatPos,math::matrix<double>& DesMatTrop,math::matrix<double>& DesMatIono,math::matrix<double>& DesMatAmb,
										 math::matrix<double>& L,DdData dddata,DdCtrl ddctrl,DdAmbInfo ambinfo,DdObsInfo ddobsinfo)
{
	FormDesMatPos( DesMatPos, L ,dddata,ddobsinfo,ddctrl);

	if (ddctrl.sysid!=2) 
	{
		FormDesMatAmb(DesMatAmb, ddctrl,dddata,ddobsinfo,ambinfo);
		FormDesMatIono(DesMatIono,ddctrl,dddata);
	}
		
	if (ddctrl.sysid==2)
	{
		FormDesMatAmbGlo(DesMatAmb,ddctrl,dddata);
		//FormDesMatIonoGlo(DesMatIono,ddctrl,dddata);
	}
	
	FormDesMatTrop( DesMatTrop, ddctrl,dddata);

	FormResidual(L,dddata,ddctrl,ambinfo);

	ReFormConstWithAmb(L,ambinfo,ddobsinfo,ddctrl,dddata);
}

/*********************************the end of Design Matrix***************************************/


/*delete the ambiguities */
void Position::PassPreAmb(DdAmbInfo pre,DdAmbInfo& cur,int typeNum)
{
	int i,j;
	for (i=0;i<typeNum;i++)
	{
		int numPre=pre.NoSat(i),numCur=cur.NoSat(i);
		int pos;
		for (j=0;j<numCur;j++)
		{
			pos=FindPosInt(pre.prnList[i],numPre,cur.prnList[i][j]);
			if(pos!=-1) 
			{
				if(pre.fixFlag[i][pos]==1)
				{
					cur.fixFlag[i][j]=1;
					cur.fixSolu[i][j]=pre.fixSolu[i][pos];
				}
			}
		}
		
	}
}
/*
	change the Predddata and Amb base on the cycle slip of refsat
	I:
		preAmbInfo
		newRef
	O:
		preAmbInfo
*/
void Position::ChangePreAmb(DdAmbInfo& preAmbInfo,int newRef,DdCtrl ddctrl)
{
	int pos,i,j,k;
	int num	=(ddctrl.ddambctrl.flag-3>0)?ddctrl.ddambctrl.flag-3	:ddctrl.ddambctrl.flag;
	
	for (i=0;i<num;i++)
	{
		pos=preAmbInfo.FindSat(i,newRef);
		preAmbInfo.prnList[i][pos]=preAmbInfo.refSate;
		int loop=preAmbInfo.NoSat(i);
		for (j=0;j<loop;j++)
		{
			preAmbInfo.fixSolu[i][j]=(pos==j)?-preAmbInfo.fixSolu[i][j]:preAmbInfo.fixSolu[i][j]-preAmbInfo.fixSolu[i][pos];
		}
	}
	preAmbInfo.refSate=newRef;
}


void Position::ChangePredataByNewRef(DdData& predddata,int newRef)
{
	//DdData temp(predddata.pairNum);
	DdData temp;
	temp=predddata;
	int index	=-1;
	for (int i=0;i<predddata.pairNum;i++)
	{
		if (predddata.rovPrn[i]==newRef)
		{
			index=i;
		}
	}
	temp.refPrn			=newRef;
	temp.rovPrn[index]	=predddata.refPrn;
 	for (int i=0;i<NFREQ+NEXOBS;i++)
	{
		temp.datarecord[index].Phase[i]		=-predddata.datarecord[index].Phase[i];
		temp.datarecord[index].PsRange[i]	=-predddata.datarecord[index].PsRange[i];
	}
	for (int i=0;i<3;i++)
	{
		temp.refSatPos_Base[i]						=predddata.satePosBase[index].sateXYZ[i];
		temp.refSatPos_Rov[i]						=predddata.satePosRov[index].sateXYZ[i];
		temp.satePosBase[index].sateXYZ[i]	=predddata.refSatPos_Base[i];
		temp.satePosRov[index].sateXYZ[i]	=predddata.refSatPos_Rov[i];
	}
		

	for (int i=0;i<predddata.pairNum;i++)
	{
		if (i!=index)
		{
			for (int j=0;j<NFREQ+NEXOBS;j++)
			{
				temp.datarecord[i].Phase[j]		=predddata.datarecord[i].Phase[j]			-predddata.datarecord[index].Phase[j];
				temp.datarecord[i].PsRange[j]	=predddata.datarecord[i].PsRange[j]		-predddata.datarecord[index].PsRange[j];
			}	
		} 
	}
	predddata	=temp;
}

/*
 *form the vc matrix of combination obs
 *I:
 *	coef k*k	the coef of combination   k=1,2,3
 *	Qyy	the cofactor or vc mat of non-combined obs 
 *	flag	the number of obs = k
 *O:
 *	Qyy		the vc mat of  combination 	
 */
void Position::VcmatCom(math::matrix<double> coef,math::matrix<double>& Qyy,int flag)
{
	math::matrix<double>temp(flag,flag);
	temp=Qyy;
	Qyy	=coef*temp*(~coef);
}
/*
	Find the closest Eph to time t
	I:
		ephdata	include all sate ephemeris data
		ephNum  number of ephemeris data 
		prn           
		t				second of week 
	O:
		ephpos   the position of eph wanted,-1 if not found
	return:		false if not found
	Note:		"t" should be in the same system as the time in ephdata
	
*/
bool Position::FindEph(BroadEphData* ephdata,int ephNum, int prn,double t,int& ephpos)
{
	ephpos=-1;
	double predte;
	for (int i=0;i<ephNum;i++)
	{
		if (ephdata[i].prn!=prn || ephdata[i].satClkBs==0.0 || ephdata[i].sqA==0.0)
		{
			continue;
		}
		else
		{
			double dte;
			dte=timediff(t,ephdata[i].toe);//time diff  mark
			dte	= fabs(dte);
			if (dte>3600.0*2)//in 2 hours
			{
				continue;
			}
			if (ephpos==-1) 
			{
					ephpos=i;
					predte=dte;
			}
			if(dte<predte)
			{
				predte=dte;
				ephpos=i;
			}

		}
	}//end for

	if(ephpos==-1)
	{
		return false;
	}
	else
	{
		if (predte>3600.0*4 || fabs(timediff(t,ephdata[ephpos].toc))>3600.0*4)
		{
			return false;
		}
		if(ephdata[ephpos].satHealth!=0) 
		{
			return false;
		}
	}
	return true;
}

bool Position::FindEph(BroadEphDataGlo* ephdata,int ephNum, int prn,double t,int& ephpos)
{
	ephpos=-1;
	double predte;
	for (int i=0;i<ephNum;i++)
	{
		if (ephdata[i].prn!=prn)
		{
			continue;
		}
		else
		{
			double dte;
			dte=timediff(t,ephdata[i].toe);
			dte	= fabs(dte);
			if (dte>3600)//in one hour
			{
				continue;
			}
			if (ephpos==-1) 
			{
				ephpos=i;
				predte=dte;
			}
			if(dte<predte)
			{
				predte=dte;
				ephpos=i;
			}

		}
	}//end for

	if(ephpos==-1)
	{
		return false;
	}
	else
	{
		if (predte>3600*4.0 || fabs(t-ephdata[ephpos].toc)>3600*24.0)
		{
			return false;
		}
	}
	return true;
}
/*
	calibrate the receiver clock error and tropsphere delay(UNB3)
*/
void Position::CalibRecClkErr(ObsEpochData& lastData,SppInfo sppinfo)
{
	for(int i=0;i<lastData.sateNum;i++)
	{
			
		for(int j=0;j<NFREQ+NEXOBS;j++)
		{
			double freq=Freq(lastData.obsdatarecord->PRN, j);//
			double lamda=CLIGHT/freq;//////add the lamda
			
			lastData.obsdatarecord[i].Phase[j]			=lastData.obsdatarecord[i].Phase[j]-sppinfo.tropCorr[i]/lamda-sppinfo.dtr/lamda;

			lastData.obsdatarecord[i].PsRange[j]		=lastData.obsdatarecord[i].PsRange[j]-sppinfo.tropCorr[i]-sppinfo.dtr;
		
		}

	}
}

/*
	calibrate the sate clock error 
*/
void Position::CalibSateClkErr(ObsEpochData& lastData,SppInfo sppinfo)
{
	for(int i=0;i<lastData.sateNum;i++)
	{
			
		for(int j=0;j<NFREQ+NEXOBS;j++)
		{
			double freq=Freq(lastData.obsdatarecord[i].PRN, j);//
			double lamda=CLIGHT/freq;//////add the lamda
			
			lastData.obsdatarecord[i].Phase[j]			=lastData.obsdatarecord[i].Phase[j]+sppinfo.sateclkerr[i]*lamda;

			lastData.obsdatarecord[i].PsRange[j]		=lastData.obsdatarecord[i].PsRange[j]+sppinfo.sateclkerr[i]*CLIGHT;
		
		}

	}
}

/*
 *get the obs wanted.
 *due to the mixture of multi-system, get the obs we want
 *I:
 *	
 *O:
 *
 *Return
 */
ObsEpochData Position:: GetSysData(int sysid,ObsEpochData epochdata)
{
	int prnfirst=1+(sysid-1)*50;
	int prnlast=prnfirst+50;
	int count=0;
	int* pos=new int[epochdata.sateNum];
	for (int i=0;i<epochdata.sateNum;i++)
	{
		if (epochdata.obsdatarecord[i].PRN>=prnfirst &&epochdata.obsdatarecord[i].PRN<prnlast)
		{
			pos[count]=i;
			count++;
		}
	}
	ObsEpochData temp;
	temp.sateNum=count;
	temp.flag=epochdata.flag;
	temp.sec=epochdata.sec;
	temp.week=epochdata.week;
	for (int i=0;i<count;i++)
	{
		temp.obsdatarecord[i]=epochdata.obsdatarecord[pos[i]];
	}
	delete[] pos;
	return temp;
}
/*
	single point position
	I:
		ephheader	determine the ionos correction 
		ephdata		all eph data in N file 
		epochdata	all obs data of one epoch
		t					the obs time
		sppctrl			include the flag of ionosphere dalay		0=IF  1=Klobuchar  2=fixed(defalut) 3=float
							the mask elevation (default:10.0 deg)
							index of frequency								

	O:
		sppinfo			the information of SPP, including the postion of receiver , the positon of all sates used,
							all the emission time of signal and the prn list of all sates used
	NOTE��
		correct the tropsphere delay with UNB3 by default 
		observation model:	PRobs		= rho+Cdtr-Cdts+T+I+����
*/
bool Position::StandPosition(BroadEphHeader ephheader,BroadEphData* ephdata,ObsEpochData epochdata,int ephNum,SppCtrl sppctrl,SppInfo& sppinfo,ObsEpochData& lastData)
{
	//CalcSatePos	calcsate;
   // InitPtr(sppinfo.recPos,3);
	double freq[3];
	Freq(sppctrl.sysid,	freq);

	double t			=epochdata.sec;
	//double dtr		=0.0;
	int loopNum		=0;
	bool	isOutLoop	=false;

	lastData			=GetSysData(sppctrl.sysid,epochdata);//

	BroadEphIonoCorr		ionopara;
	if(sppctrl.sysid==5)	ionopara=	ephheader.ionoCorrBDS;
	else if (sppctrl.sysid==1)	ionopara=	ephheader.ionoCorrGPS;

	double pos[3]; //global variable  ,it stands for different variable
	InitPtr(pos,3);
	int column	=4;
	Trops trops;
	Ionos ionos;

	int PsrangeID=0; //the id of Psrange that used for satepos, defaul=0
	/* compute all satepos and the lists of sppinfo and obsdata are the same*/
	int count=0;
	math::matrix<double> positionSate(3,1);
	math::matrix<double>	satevel(3,1);
	
	
	double   ratiofreq=0.0;
	if (sppctrl.ionoFlag!=0)	ratiofreq=FreqSys(sppctrl.sysid,0)/FreqSys(sppctrl.sysid,sppctrl.freqIndex);
	
	int loopctrl=3;// >=3
	int isShrink=0;
	for(int i=0;i<lastData.sateNum;i++)
	{
		int ephpos=-1,valid=0;
		double transtime=0;
		if( FindEph(ephdata,ephNum,lastData.obsdatarecord[i].PRN,t,ephpos) )
		{
			sppinfo.codeCorr[count]=ephdata[ephpos].tgd*ratiofreq;
			transtime =lastData.sec-lastData.obsdatarecord[i].PsRange[PsrangeID]/CLIGHT;
			sppinfo.sateclkerr[count]=ephclkerr(transtime,ephdata[ephpos]);
			transtime-=sppinfo.sateclkerr[count];
			satepos(positionSate,ephdata[ephpos],transtime,sppinfo.sateclkerr[count],satevel,sppinfo.sateclkVel[count]);
			for(int j=0;j<3;j++)
			{
				sppinfo.satePos[count].sateXYZ[j]=positionSate(j,0);
				sppinfo.sateVel[count].sateXYZ[j]	=satevel(j,0);
			}
			sppinfo.prnList[count]		=lastData.obsdatarecord[i].PRN;
			count++;
			valid=1;
		}
		else
		{
			lastData.obsdatarecord[i].ZeroElem();
			isShrink=1;
		}
	}
	sppinfo.validnum	=count;
	lastData.sateNum	=count;
	if(isShrink==1) lastData=lastData.Shrink();
	if (count<4)	return false;

	while( ! isOutLoop)
	{
		if(loopNum==loopctrl)
			{
				/*	select the satellites based on elevation	*/
				if (! SelectSateOnEle(sppctrl.maskEle,sppinfo, lastData) ) return false;//if satenum left<4,return false
			}
		
		count=sppinfo.validnum;
		/* weight matrix and y vector*/
		math::matrix<double> weightMat(count,count);
		math::matrix<double> yObs(count,1);

		/*  design matrix  */
		column	=(sppctrl.ionoFlag==3?5:4);
		math::matrix<double> desMat(count,column);
		
		//design and weight matrix 
		for(int i=0;i<count;i++)
		{
			double obs	=0.0;
			for (int ii=0;ii<3;ii++)	pos[ii]=sppinfo.satePos[i].sateXYZ[ii];
			if (loopNum>1) XYZ2RAH(sppinfo.recPos,sppctrl.sysid,pos,sppinfo.ele[i],sppinfo.azi[i]);
			weightMat(i,i)				=loopNum>1?weightfactor(sppinfo.ele[i],1)  :1.0;//SQ(sppinfo.ele[i])
			weightMat(i,i)				=1.0;
			double		rsRange		=0.0;
			for(int j=0;j<3;j++)	pos[j]		=sppinfo.recPos[j]-sppinfo.satePos[i].sateXYZ[j];
			rsRange	=Norm(pos,3);
			for(int j=0;j<3;j++) desMat(i,j)=pos[j]/rsRange;
			desMat(i,3)	=1.0;
				
			// sagnac effect(earth rotation) correction on distance
			if (loopNum>0) rsRange+=geodistcorr(sppinfo.satePos[i],sppinfo.recPos);
			
			//ionosphere part  
			if(sppctrl.ionoFlag==3) desMat(i,5)=SQ(freq[0])/SQ(freq[sppctrl.freqIndex]);
		
			int coef[3];
			InitPtr(coef,3);
			for(int j=0;j<3;j++) pos[j]=lastData.obsdatarecord[i].PsRange[j];
			if(sppctrl.freqIndex!=0)
			{
				coef[sppctrl.freqIndex-1]		=1;
				obs										=lastData.obsdatarecord[i].PsRange[sppctrl.freqIndex-1];
			}
			else//combine the IF observable
			{
				int dualIndex[2];
				dualIndex[0]=sppctrl.IFIndex[0];
				dualIndex[1]=sppctrl.IFIndex[1];
				if (lastData.obsdatarecord[i].PsRange[dualIndex[0]]>0.0 &&lastData.obsdatarecord[i].PsRange[dualIndex[1]]>0.0)
				{
					obs		=IonoFree(sppctrl.sysid,pos,dualIndex);
				}
				else  // if Lc obs can not be formed, using the original obs
				{
					int ci=0;
					while (1)
					{
						obs=lastData.obsdatarecord[i].PsRange[ci++];
						if (obs>0.0) break;
					}
				}
			}
			//ionosphere correction
			obs	-=		(sppctrl.ionoFlag==1 && loopNum>1  )?
													ionos.Klobuchar(ionopara,epochdata.sec,sppinfo.recPos, sppinfo.azi[i], sppinfo.ele[i])  : 0.0;
		
		
			//troposphere correction
				XYZ2BLH(sppinfo.recPos,Sysid(sppinfo.prnList[i]),pos);
				sppinfo.tropCorr[i]=loopNum>1? trops.TropsUNB3(sppinfo.ele[i],pos[0]*R2D,pos[2],doy(sppctrl.sysid,lastData.week,lastData.sec),sppinfo.mapWet[i]) :0.0;
				if(fabs(sppinfo.tropCorr[i])>100.0) sppinfo.tropCorr[i]=0.0;
				obs	-=		sppinfo.tropCorr[i];
				//cout<<trops.TropsUNB3(sppinfo.ele[i],pos[0]*R2D,pos[2],doy(sppctrl.sysid,lastData.week,lastData.sec),sppinfo.mapWet[i])<<endl;
			//sate err &&	//code correction
			obs	+=	CLIGHT*(sppinfo.sateclkerr[i]-sppinfo.codeCorr[i]);
			obs	-=		sppinfo.dtr;
			yObs(i,0)					=obs-rsRange;
			/* obs-(rsRange+tropscorr+ionocorr+CLIGHT*dtr0-CLIGHT*sateErr) */
		}
		math::matrix<double> xhat(column,1);
		xhat=CholeskyInv((~desMat)*weightMat*desMat)* ( (~desMat)*weightMat*yObs );
		for(int i=0;i<3;i++)
		{	
			sppinfo.recPos[i]	+=xhat(i,0);
			pos[i]=xhat(i,0);
		}
		sppinfo.dtr	+=xhat(3,0);
		
		if( (loopNum>0 && AbsMax(pos,3)<1e-5 && xhat(3,0)<1e-5) || loopNum>9 )
		{
			isOutLoop=true;
			math::matrix<double>d=GetBlockMat(CholeskyInv(~desMat*desMat),1,3,1,3,2);
			sppinfo.gdop=traceMat(d);
			sppinfo.gdop=sqrt(sppinfo.gdop);
			//math::matrix<double> residual;
			//residual=yObs-desMat*xhat;
			/* cout<<"------------residual---------------"<<endl;
			cout<<~(residual); */
#if OUTSPP==1
			for(int i=0;i<3;i++)
			{	
				cout<<setiosflags(ios::fixed)<<setprecision(3)<<setw(14)<<sppinfo.recPos[i]<<"   ";
			}
			cout<<sppinfo.dtr;
			cout<<endl;cout<<endl;//cout<<endl;
#endif
		}
		loopNum++;	
	}

	return true;
}

bool Position:: baseStn(SppInfo& baseStnInfo,BroadEphData* ephdata,ObsEpochData epochdata,ObsEpochData& lastData,int ephNum,SppCtrl baseCtrl)
{
	// select the data
	lastData			=GetSysData(baseCtrl.sysid,epochdata);

	//satepos
	if (!calcAllSatePos(baseStnInfo,ephdata,lastData,ephNum)) return false;

	Trops trops;
	// calc ele, azi, tropo corr 
	double pos[3];int cnt=0;
	for (int i=0;i<baseStnInfo.validnum;i++)
	{
		for (int j=0;j<3;j++) pos[j]=baseStnInfo.satePos[i].sateXYZ[j];
		XYZ2RAH(baseStnInfo.recPos,baseCtrl.sysid,pos,baseStnInfo.ele[i],baseStnInfo.azi[i]);
		XYZ2BLH(baseStnInfo.recPos,Sysid(baseStnInfo.prnList[i]),pos);
		baseStnInfo.tropCorr[i]=trops.TropsUNB3(baseStnInfo.ele[i],pos[0]*R2D,pos[2],doy(baseCtrl.sysid,lastData.week,lastData.sec),baseStnInfo.mapWet[i]);
	}
	
	/*	select the satellites based on elevation  */
	if (!SelectSateOnEle(baseCtrl.maskEle,baseStnInfo, lastData) ) return false;//if satenum left<4,return false

	return true;
}

bool Position:: calcAllSatePos(SppInfo& baseStnInfo,BroadEphData* ephdata,ObsEpochData& lastData,int ephNum)
{
	int count=0;
	math::matrix<double> positionSate(3,1);
	math::matrix<double>	satevel(3,1);
	int PsrangeID=0,isShrink=0;
	for (int i=0;i<lastData.sateNum;i++)
	{
		int ephpos=-1;
		double transtime=0;
		if( FindEph(ephdata,ephNum,lastData.obsdatarecord[i].PRN,lastData.sec,ephpos) )
		{
			//baseStnInfo.codeCorr[count]=ephdata[ephpos].tgd*ratiofreq;
			//cout<<ephdata[ephpos].tgd<<endl;//sppinfo.codeCorr[count]*1E9
			transtime =lastData.sec-lastData.obsdatarecord[i].PsRange[PsrangeID]/CLIGHT;
			double clks=ephclkerr(transtime,ephdata[ephpos]);
			baseStnInfo.sateclkerr[count]=clks;
			transtime-=baseStnInfo.sateclkerr[count];
			satepos(positionSate,ephdata[ephpos],transtime,baseStnInfo.sateclkerr[count],satevel,baseStnInfo.sateclkVel[count]);
			for(int j=0;j<3;j++)
			{
				baseStnInfo.satePos[count].sateXYZ[j]=positionSate(j,0);
				baseStnInfo.sateVel[count].sateXYZ[j]	=satevel(j,0);
			}
			baseStnInfo.prnList[count]		=lastData.obsdatarecord[i].PRN;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      
			count++;
		}
		else
		{
			lastData.obsdatarecord[i].ZeroElem();
			isShrink=1;
		}
	}
	baseStnInfo.validnum	=count;
	lastData.sateNum	=count;
	if(isShrink==1)lastData= lastData.Shrink();
	return count<4?false:true;
}

/* Glonass  SPP */
bool Position::StandPosition(BroadEphHeader ephheader,BroadEphDataGlo* ephdata,ObsEpochData epochdata,int ephNum,SppCtrl sppctrl,SppInfoGlo& sppinfo,ObsEpochData& lastData)
{
	InitPtr(sppinfo.recPos,3);
	int i,j,k;
	lastData			=GetSysData(2,epochdata);
	//set the frequency number
	int freqnum[MAXNUMSATE_GLO];
	for (i=0;i<lastData.sateNum;i++)
	{
		for (j=0;j<ephNum;j++)
		{
			if (lastData.obsdatarecord[i].PRN==ephdata[j].prn)
			{
				freqnum[i]=ephdata[j].freqNum;
				break;
			}
		}
	}

	double pos[3];

	double t=epochdata.sec;//gpst
	int PsrangeID=0;
	//calc satepos and sateclk
	int count=0;
	for(i=0;i<lastData.sateNum;i++)
	{
		int ephpos=-1;
		double transtime=0.0;
		if( FindEph(ephdata,ephNum,lastData.obsdatarecord[i].PRN,t,ephpos) )
		{
			transtime =lastData.sec-lastData.obsdatarecord[i].PsRange[PsrangeID]/CLIGHT;
			sppinfo.sateclkerr[count]=geph2clk(transtime,ephdata[ephpos]);
			transtime-=sppinfo.sateclkerr[count];
			geph2pos(transtime,ephdata[ephpos],pos,sppinfo.sateclkerr[count]);
			//cout<<pos[0]<<"  "<<pos[1]<<"  "<<pos[2]<<endl;
			for( j=0;j<3;j++)
			{
				sppinfo.satePos[count].sateXYZ[j]=pos[j];
				//sppinfo.sateVel[count].sateXYZ[j]	=satevel(j,0);
			}
			sppinfo.prnList[count]		=lastData.obsdatarecord[i].PRN;
			sppinfo.freqNum[count]	=freqnum[i];
			count++;
		}
	}
	sppinfo.validnum	=count;
	lastData.sateNum	=count;
	if (count<4)	return false;

	Trops trops;
	int loopctrl=3;// >=3
	int loopNum=0;
	bool isOutLoop=false;
	int column=0;
	while( ! isOutLoop)
	{
		
		if(loopNum==loopctrl)
			{
				/*	select the satellites based on elevation, and change the lastdata 	*/
				if (! SelectSateOnEleGlo(sppctrl.maskEle,sppinfo, lastData) ) return false;
			}
		
		count=sppinfo.validnum;
		/* weight matrix and y vector*/
		math::matrix<double> weightMat(count,count);
		math::matrix<double> yObs(count,1);

		/*  design matrix  */
		column	=(sppctrl.ionoFlag==3?5:4);//ionoflag=3 ionoshphere float equivalent to IF, 2 or more type observations 
		math::matrix<double> desMat(count,column);
		//design and weight matrix 
		for(i=0;i<count;i++)
		{
			double obs	=0.0;
			for (int ii=0;ii<3;ii++)	pos[ii]=sppinfo.satePos[i].sateXYZ[ii];
			if (loopNum>1) XYZ2RAH(sppinfo.recPos,2,pos,sppinfo.ele[i],sppinfo.azi[i]);
			
			weightMat(i,i)				=loopNum>1?weightfactor(sppinfo.ele[i],1)  :1.0;//SQ(sppinfo.ele[i])
			double		rsRange		=0.0;
			for( j=0;j<3;j++)	pos[j]		=sppinfo.recPos[j]-sppinfo.satePos[i].sateXYZ[j];
			rsRange	=Norm(pos,3);
			for( j=0;j<3;j++) desMat(i,j)=pos[j]/rsRange;
			desMat(i,3)	=1.0;
					
			if (loopNum>1)
			{
				// sagnac effect(earth rotation) correction on distance
				double sagnac=geodistcorr(sppinfo.satePos[i],sppinfo.recPos);
				rsRange+=sagnac;//>0.0?sagnac:0.0;
			}
			
			//ionospere part  
			//if(sppctrl.ionoFlag==3) desMat(i,5)=SQ(freq[0])/SQ(freq[sppctrl.freqIndex]);
		
			for( j=0;j<3;j++)
			{
				pos[j]=lastData.obsdatarecord[i].PsRange[j];
			}
			if(sppctrl.freqIndex!=0)
			{
				obs		=lastData.obsdatarecord[i].PsRange[sppctrl.freqIndex-1];
			}
			else//combine the IF observable
			{
				int dualIndex[2];
				dualIndex[0]=sppctrl.IFIndex[0];
				dualIndex[1]=sppctrl.IFIndex[1];
				obs		=IonoFreeGlo(pos,dualIndex,sppinfo.freqNum[i]);
			}
			//ionosphere correction
			/* obs	-=		(sppctrl.ionoFlag==1 && loopNum>1  )?
													ionos.Klobuchar(ionopara,epochdata.sec,sppinfo.recPos, sppinfo.azi[i], sppinfo.ele[i])  : 0.0; */
		
			//troposphere correction
				XYZ2BLH(sppinfo.recPos,Sysid(sppinfo.prnList[i]),pos);
				sppinfo.tropCorr[i]=loopNum>1? trops.TropsUNB3(sppinfo.ele[i],pos[0]*R2D,pos[2],doy(sppctrl.sysid,lastData.week,lastData.sec),sppinfo.mapWet[i]) :0.0;
				obs	-=		sppinfo.tropCorr[i];
			//sate err &&	//code correction
			obs	+=	CLIGHT*(sppinfo.sateclkerr[i]-sppinfo.codeCorr[i]);
			obs	-=		sppinfo.dtr;
			/* negative pseudorange residual */
			yObs(i,0)					=obs-rsRange;
			/* obs-(rsRange+tropscorr+ionocorr+CLIGHT*dtr0-CLIGHT*sateErr) */
			 
		}
		math::matrix<double> xhat(column,1);
		xhat=CholeskyInv((~desMat)*weightMat*desMat)* ( ((~desMat)*weightMat*yObs) );
		
		
		for( i=0;i<3;i++)
		{	
			sppinfo.recPos[i]	+=xhat(i,0);
			pos[i]=(xhat(i,0));
		}
		sppinfo.dtr	+=xhat(3,0);
		
		if( (loopNum>0 && AbsMax(pos,3)<1e-5) || loopNum>9 )
		{
			isOutLoop=true;
			
			math::matrix<double> residual;
			residual=yObs-desMat*xhat;
		/*	cout<<"------------residual---------------"<<endl;
			cout<<~(residual);*/
			for( i=0;i<3;i++)
			{	
				cout<<setiosflags(ios::fixed)<<setprecision(3)<<setw(14)<<sppinfo.recPos[i]<<"   ";
			}
			cout<<sppinfo.dtr;
			cout<<endl;cout<<endl;//cout<<endl;

		}
		loopNum++;	
	}
	return true;
}

bool Position:: baseStn(SppInfoGlo& baseStnInfo,BroadEphDataGlo* ephdata,ObsEpochData epochdata,ObsEpochData& lastData,int ephNum,SppCtrl baseCtrl)
{
	// select the data
	lastData			=GetSysData(2,epochdata);//baseCtrl.sysid

	//satepos
	if (!calcAllSatePos(baseStnInfo,ephdata,lastData,ephNum))	return false;

	Trops trops;
	// calc ele, azi, tropo corr 
	double pos[3];int cnt=0;
	for (int i=0;i<baseStnInfo.validnum;i++)
	{
		for (int j=0;j<3;j++)
		{
			pos[j]=baseStnInfo.satePos[i].sateXYZ[j];
		}
		XYZ2RAH(baseStnInfo.recPos,baseCtrl.sysid,pos,baseStnInfo.ele[i],baseStnInfo.azi[i]);
		XYZ2BLH(baseStnInfo.recPos,Sysid(baseStnInfo.prnList[i]),pos);
		baseStnInfo.tropCorr[i]=trops.TropsUNB3(baseStnInfo.ele[i],pos[0]*R2D,pos[2],doy(baseCtrl.sysid,lastData.week,lastData.sec),baseStnInfo.mapWet[i]);
	}

	/*	select the satellites based on elevation  */
	if (!SelectSateOnEleGlo(baseCtrl.maskEle,baseStnInfo, lastData) )	return false;

	return true;
}

bool Position:: calcAllSatePos(SppInfoGlo& baseStnInfo,BroadEphDataGlo* ephdata,ObsEpochData& lastData,int ephNum)
{
	int count=0,i,j;

	int freqnum[MAXNUMSATE_GLO];
	for (i=0;i<lastData.sateNum;i++)
	{
		for (j=0;j<ephNum;j++)
		{
			if (lastData.obsdatarecord[i].PRN==ephdata[j].prn)
			{
				freqnum[i]=ephdata[j].freqNum;
				break;
			}
		}
	}

	double pos[3];
	int PsrangeID=0;
	for (i=0;i<lastData.sateNum;i++)
	{
		int ephpos=-1;
		double transtime=0;
		if( FindEph(ephdata,ephNum,lastData.obsdatarecord[i].PRN,lastData.sec,ephpos) )
		{
			//baseStnInfo.codeCorr[count]=ephdata[ephpos].tgd*ratiofreq;
			//cout<<ephdata[ephpos].tgd<<endl;//sppinfo.codeCorr[count]*1E9
			transtime =lastData.sec-lastData.obsdatarecord[i].PsRange[PsrangeID]/CLIGHT;
			baseStnInfo.sateclkerr[count]=geph2clk(transtime,ephdata[ephpos]);
			transtime-=baseStnInfo.sateclkerr[count];
			geph2pos(transtime,ephdata[ephpos],pos,baseStnInfo.sateclkerr[count]);
			for( j=0;j<3;j++)
			{
				baseStnInfo.satePos[count].sateXYZ[j]=pos[j];
				//sppinfo.sateVel[count].sateXYZ[j]	=satevel(j,0);
			}
			baseStnInfo.prnList[count]		=lastData.obsdatarecord[i].PRN;
			baseStnInfo.freqNum[count]	=freqnum[i];
			count++;
		}
	}
	baseStnInfo.validnum	=count;
	lastData.sateNum	=count;
	return count<4?false:true;
}
/* end Glonass*/

/*
 *The main of this class includes SPP, cycle slip, relative position...
 *I:
 *	BaseData		the Base station data
 *	RoverData		the rover station data
 *	ephheader	the ephemeris header
 *	ephdata		the ephemeris data, not including the GLONASS(2015.8.5)
 *	nSate			the number of satellites in ephdata
 *	sppctrl			the spp control 
 *	sppinfo			the rover station info of spp
 *	baseinfo		the base station info
 *O:
 *	NULL		reserve for the undefined input and output data, to be constructed
 *Note:
 *
 */
int Position::PositionMain( ObsEpochData epochData1, ObsEpochData epochData2, BroadEphHeader ephheader, BroadEphData* ephdata,int nSate,
									SppCtrl sppctrl,SppInfo& sppinfo,SppInfo& baseInfo)
{
	
	ObsEpochData BaseData,RoverData;
	DdData dddataPre;
	DdData dddataCurr;
	SdData lastSdData;
	
	StandPosition(ephheader,ephdata,epochData1,nSate,sppctrl,sppinfo,RoverData);

	baseStn(baseInfo,ephdata,epochData2,BaseData,nSate,sppctrl);

	for (int ij=0;ij<sppinfo.validnum;ij++)
	{
		cout<<sppinfo.ele[ij]<<"  ";
	}

	int refPrn=0;
//	SelectRefSate(sppinfo,baseInfo,10.0,BaseData,RoverData,lastSdData,dddataCurr);

	for (int ij=0;ij<sppinfo.validnum;ij++)
	{
		cout<<sppinfo.ele[ij]<<"  ";
	}

	return 1;
}

/*****************************************post  process********************************************************/
/*
 *post precess data        temporarily
 *
 */

/*Qdd is Pdd*/
void Position:: CrossCode( int refpos,SdData lastSdData,	math::matrix<double>&Qdd0,math::matrix<double>&Qdd1)
{
	double c0=0.330,c1=0.380;
	double c02=0.250,c12=0.270;
	math::matrix<double>Qzd(lastSdData.satnum,lastSdData.satnum);
	for (int ik=0;ik<lastSdData.satnum;ik++) Qzd(ik,ik)=SQ( c0/(sin(lastSdData.ele[ik])+c1));
	
	math::matrix<double>Qzd1(lastSdData.satnum,lastSdData.satnum);
	for (int ik=0;ik<lastSdData.satnum;ik++) Qzd1(ik,ik)=SQ( c02/(sin(lastSdData.ele[ik])+c12));

	math::matrix<double>Qsd,QsdCross;
	Qsd=2.0*DiagMatSym(Qzd,Qzd1);
	QsdCross=Qsd;
	for (int ik=0;ik<lastSdData.satnum;ik++)
	{
		QsdCross(ik,ik+lastSdData.satnum)=2.0*( c0/(sin(lastSdData.ele[ik])+c1))*( c02/(sin(lastSdData.ele[ik])+c12))*0.530;
		QsdCross(ik+lastSdData.satnum,ik)=2.0*( c0/(sin(lastSdData.ele[ik])+c1))*( c02/(sin(lastSdData.ele[ik])+c12))*0.530;
	}

	math::matrix<double>Df; Df=EyeMat(lastSdData.satnum);
	Df=RemoveRow(Df,refpos+1);
	SetMatClo(Df,refpos+1,-1.0);

	math::matrix<double>I,Qddcross;
	I=EyeMat(2);
	Df=Kronecker( I,Df, 4);
	Qdd0=Df*Qsd*(~Df);

	Qddcross=Df*QsdCross*(~Df);
	/*Qzd=2.0*Df*Qzd*(~Df);
	Qzd1=2.0*Df*Qzd1*(~Df);
	Qdd0=DiagMatSym(Qzd,Qzd1);*/
	Qdd0=CholeskyInv(Qdd0);

	/*I(0,1)=0.53;I(1,0)=0.53;
	math::matrix<double>Qsd1;
	Qsd1=2.0*Kronecker(I,Qzd,4);
	*/
	/*I(0,1)=0.0;I(1,0)=0.0;
	Qdd1=Kronecker(I,Df,4)*Qsd1*(~Kronecker(I,Df,4));*/
	Qdd1=CholeskyInv(Qddcross);
}

void Position:: PostProcess0916(int numEpoch,SdData* sdData,int* satprn,DdCtrl ddctrl)
 {
	 int maxNum=1650,  interval=20;
	 math::matrix<double>Ne;//=new math::matrix<double>[maxNum];
	 math::matrix<double>Le;//=new math::matrix<double>[maxNum];
	 math::matrix<double>Nr;//=new math::matrix<double>[maxNum];
	 math::matrix<double>Lr;//=new math::matrix<double>[maxNum];
	 //=new math::matrix<double>[maxNum];
	DdAmbInfo ambinfo;
	 int i,j,k,cnt=0;

	 
	 math::matrix<double> Rota(3,3);
	 double BLH[3],XYZ[3];
	 XYZ[0]=-2364333.4909;XYZ[1]=4870287.3243;XYZ[2]=-3360809.5628; 
	 math::matrix<double> xt(3,1);
	xt(0,0)=-2364333.4909;xt(1,0)=4870287.3243;xt(2,0)=-3360809.5628; 
	 XYZ2BLH(XYZ,1,BLH);
	 double sinB=sin(BLH[0]),cosB=cos(BLH[0]);
	 double sinL=sin(BLH[1]),cosL=cos(BLH[1]);
	 Rota(0,0)=-sinB*cosL;	Rota(0,1)=-sinB*sinL;		Rota(0,2)=cosB;
	 Rota(1,0)=-sinL;				Rota(1,1)=cosL;				Rota(1,2)=0.0;
	 Rota(2,0)=cosL*cosB;		Rota(2,1)=cosB*sinL;		Rota(2,2)=sinB;
	 
	 math::matrix<double>sige;
	 math::matrix<double>sigr;

	 math::matrix<double>eta0;
	 math::matrix<double>eta1;
	 fstream fout;
	 fout.open("resultCross.txt",ios::out);
	 for (i=0;i<maxNum;i++)
	 {
		 if (maxNum==50000) break;
		 int referPrn=satprn[i];
		 if(sdData[i].satnum==0)  continue;
		 if (i==maxNum-interval-1) break;
		 math::matrix<double>Pdd0;
		 math::matrix<double>Pdd1;
		 CrossCode(  GetPos(sdData[i].prn,referPrn,sdData[i].satnum),sdData[i],Pdd0,Pdd1 );
		// Ne[i]=ZeroMat(3,3); Le[i]=ZeroMat(3,1);Nr[i]=ZeroMat(3,3); Lr[i]=ZeroMat(3,1);
		// for (j=i;j<i+interval;j++)
		// {
		    j=i;
			 
			 int referPos=GetPos(sdData[j].prn,referPrn,sdData[j].satnum);
			 DdData dddataCurr;	 DdObsInfo ddobsinfo;
			 DoubleDiff(referPrn,referPos,sdData[i],dddataCurr);
			 dddataCurr=ComObsPhsCod(ddctrl,ddobsinfo,ambinfo,dddataCurr);
			 if(sdData[i].satnum-dddataCurr.pairNum!=1) continue;
			 int obsNum	=ddobsinfo.SumCod();//the number of all obs in one system at current epoch
			math::matrix<double>y(obsNum,1);
			math::matrix<double>V(obsNum,1);
			 math::matrix<double> DesMatPos(obsNum,3);
			 math::matrix<double> L(obsNum,1);
			 FormDesMatPos(DesMatPos,L,dddataCurr,ddobsinfo,ddctrl);
			 FormResidual(L,y,dddataCurr,ddctrl,ambinfo);

			 if (Pdd0.RowNo()!=DesMatPos.RowNo() ||Pdd1.RowNo()!=DesMatPos.RowNo()||Pdd1.RowNo()!=L.RowNo())
			 {
				 cout<<i<<"  error!!!"<<endl;
				 continue;
			 }
			 Ne=(~DesMatPos)*Pdd0*DesMatPos;
			 Nr=(~DesMatPos)*Pdd1*DesMatPos;
			 Le=(~DesMatPos)*Pdd0*L;
			 Lr=(~DesMatPos)*Pdd1*L;
			 
		// }
		 
		 Ne=CholeskyInv(Ne);eta0=-(~Le)*Ne*Le;
			 Le=(Ne*Le);
		 cout<<~(DesMatPos*(Le+xt));
		 sige= eta0+~L*Pdd0*L;

		 Le=~XYZ2NEU(Le,BLH[0],BLH[1]);
		 Ne=~ diagElemMat(Rota*Ne*(~Rota));
		// Ne=1.0/(obsNum-3)*sige(0,0)*Ne;

		 Nr=CholeskyInv(Nr);eta1=-(~Lr)*Nr*Lr;
		 Lr=(Nr*Lr);
		 sigr= eta1+~L*Pdd1*L;

		 Lr=~XYZ2NEU(Lr,BLH[0],BLH[1]);
		 Nr=~  diagElemMat(Rota*Nr*(~Rota));
		 //Nr=sigr(0,0)/(obsNum-3)*Nr;

		 cnt++;
		 if (i%1000==0)
		 {
			 cout<<"DD: "<<i<<"/"<<40000<<endl;
		 }
	//	 cout<<i<<"  "<<Le[i].Norm()<<"  "<<Lr[i].Norm() <<endl;
		// cout<<(Le[i])<<Ne[i]<<endl;  cout<<(Lr[i])<<Nr[i]<<endl;
		
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Le(0,0);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Le(0,1);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Le(0,2);

		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Ne(0,0);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Ne(0,1);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Ne(0,2);

		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Lr(0,0);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Lr(0,1);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Lr(0,2);

		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Nr(0,0);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Nr(0,1);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Nr(0,2);
		 fout<<endl;
	 }//end i
	 
	 fout.close();
	 int how;
 }

 void Position:: TimeCrossCode( int refpos,SdData lastSdData,	math::matrix<double>&Qdd0,math::matrix<double>&Qdd1)
{
	double c0=0.370,c1=0.180;
	double c02=0.370,c12=0.180;
	math::matrix<double>Qzd(lastSdData.satnum,lastSdData.satnum);
	for (int ik=0;ik<lastSdData.satnum;ik++) Qzd(ik,ik)=SQ( c0/(sin(lastSdData.ele[ik])+c1));
	
	math::matrix<double>Qzd1(lastSdData.satnum,lastSdData.satnum);
	for (int ik=0;ik<lastSdData.satnum;ik++) Qzd1(ik,ik)=SQ( c02/(sin(lastSdData.ele[ik])+c12));

	math::matrix<double>Qsd,QsdCross;
	Qsd=2.0*Qzd;
	QsdCross=Qsd;
	//for (int ik=0;ik<lastSdData.satnum;ik++)
	//{
	//	QsdCross(ik,ik+lastSdData.satnum)=2.0*( c0/(sin(lastSdData.ele[ik])+c1))*( c02/(sin(lastSdData.ele[ik])+c12));
	//	QsdCross(ik+lastSdData.satnum,ik)=2.0*( c0/(sin(lastSdData.ele[ik])+c1))*( c02/(sin(lastSdData.ele[ik])+c12));
	//}

	math::matrix<double>Df; Df=EyeMat(lastSdData.satnum);
	Df=RemoveRow(Df,refpos+1);
	SetMatClo(Df,refpos+1,-1.0);

	math::matrix<double>I,Qddcross;
	I=EyeMat(2);
	//Df=Kronecker( I,Df, 4);
	Qdd0=Df*Qsd*(~Df);
	//cout<<Qdd0;
	//cout<<Qsd;
	Qdd0=Kronecker(I,Qdd0,4);

	//time cross
	I(0,1)=0.60;   I(1,0)=0.60;
	Qddcross=Df*QsdCross*(~Df);
	Qddcross=Kronecker(I,Qddcross,4);
	/*Qzd=2.0*Df*Qzd*(~Df);
	Qzd1=2.0*Df*Qzd1*(~Df);
	Qdd0=DiagMatSym(Qzd,Qzd1);*/
	Qdd0=CholeskyInv(Qdd0);

	/*I(0,1)=0.53;I(1,0)=0.53;
	math::matrix<double>Qsd1;
	Qsd1=2.0*Kronecker(I,Qzd,4);
	*/
	/*I(0,1)=0.0;I(1,0)=0.0;
	Qdd1=Kronecker(I,Df,4)*Qsd1*(~Kronecker(I,Df,4));*/
	Qdd1=CholeskyInv(Qddcross);
	//cout<<Qdd0;
	//cout<<endl;
	//cout<<Qdd1;
}


 void Position:: PostProcessTimeCross(int numEpoch,SdData* sdData,int* satprn,DdCtrl ddctrl)
 {
	 int maxNum=1650,  interval=20;
	 math::matrix<double>Ne;//=new math::matrix<double>[maxNum];
	 math::matrix<double>Le;//=new math::matrix<double>[maxNum];
	 math::matrix<double>Nr;//=new math::matrix<double>[maxNum];
	 math::matrix<double>Lr;//=new math::matrix<double>[maxNum];
	 //=new math::matrix<double>[maxNum];
	 DdAmbInfo ambinfo;
	 int i,j,k,cnt=0;


	 math::matrix<double> Rota(3,3);
	 double BLH[3],XYZ[3];
	 XYZ[0]=-2364333.4909;XYZ[1]=4870287.3243;XYZ[2]=-3360809.5628; 
	 math::matrix<double> xt(3,1);
	 xt(0,0)=-2364333.4909;xt(1,0)=4870287.3243;xt(2,0)=-3360809.5628; 
	 XYZ2BLH(XYZ,1,BLH);
	 double sinB=sin(BLH[0]),cosB=cos(BLH[0]);
	 double sinL=sin(BLH[1]),cosL=cos(BLH[1]);
	 Rota(0,0)=-sinB*cosL;	Rota(0,1)=-sinB*sinL;		Rota(0,2)=cosB;
	 Rota(1,0)=-sinL;				Rota(1,1)=cosL;				Rota(1,2)=0.0;
	 Rota(2,0)=cosL*cosB;		Rota(2,1)=cosB*sinL;		Rota(2,2)=sinB;

	 math::matrix<double>sige;
	 math::matrix<double>sigr;

	 math::matrix<double>eta0;
	 math::matrix<double>eta1;
	 fstream fout;
	 fout.open("resultTimeCorr.txt",ios::out);
	 for (i=0;i<maxNum;i++)
	 {
		 if (maxNum==50000) break;
		 int referPrn=satprn[i],referPrn1=satprn[i+1];
		 if(sdData[i].satnum==0 || sdData[i+1].satnum==0 )  continue;
		 if (i==maxNum-interval-1) break;
		 math::matrix<double>Pdd0;
		 math::matrix<double>Pdd1;
		 TimeCrossCode(  GetPos(sdData[i].prn,referPrn,sdData[i].satnum),sdData[i],Pdd0,Pdd1 );
		 j=i;

		 int referPos=GetPos(sdData[j].prn,referPrn,sdData[j].satnum);
		 int referPos1=GetPos(sdData[j+1].prn,referPrn1,sdData[j+1].satnum);


		 DdData dddataCurr;	 DdObsInfo ddobsinfo;
		 DoubleDiff(referPrn,referPos,sdData[i],dddataCurr);
		 dddataCurr=ComObsPhsCod(ddctrl,ddobsinfo,ambinfo,dddataCurr);

		 DdData dddataCurr1;	 DdObsInfo ddobsinfo1;
		 DoubleDiff(referPrn1,referPos1,sdData[i+1],dddataCurr1);
		 dddataCurr=ComObsPhsCod(ddctrl,ddobsinfo1,ambinfo,dddataCurr1);

		 if(sdData[i].satnum-dddataCurr.pairNum!=1  || sdData[i+1].satnum-dddataCurr1.pairNum!=1) continue;


		 int obsNum	=ddobsinfo.SumCod();//the number of all obs in one system at current epoch
		 math::matrix<double>y(obsNum,1);
		 math::matrix<double>V(obsNum,1);
		 math::matrix<double> DesMatPos(obsNum,3);
		 math::matrix<double> L(obsNum,1);
		 FormDesMatPos(DesMatPos,L,dddataCurr,ddobsinfo,ddctrl);
		 FormResidual(L,y,dddataCurr,ddctrl,ambinfo);


		 int obsNum1	=ddobsinfo1.SumCod();//the number of all obs in one system at current epoch
		 math::matrix<double>y1(obsNum,1);
		 math::matrix<double>V1(obsNum,1);
		 math::matrix<double> DesMatPos1(obsNum,3);
		 math::matrix<double> L1(obsNum,1);
		 FormDesMatPos(DesMatPos1,L1,dddataCurr1,ddobsinfo1,ddctrl);
		 FormResidual(L1,y1,dddataCurr1,ddctrl,ambinfo);
		 
		 if(L1.RowNo()!=L.RowNo()) continue;
		 math::matrix<double>RLe,RMate;
		 RLe=VecMat(1,L,L1);
		 RMate=VecMat(3,DesMatPos,DesMatPos1);


		 if (Pdd0.RowNo()!=RLe.RowNo() ||Pdd1.RowNo()!=RLe.RowNo()||Pdd1.RowNo()!=RLe.RowNo())
		 {
			 cout<<i<<"  error!!!"<<endl;
			 continue;
		 }
		// Ne=(~RMate)*Pdd0*RMate; Le=(~RMate)*Pdd0*RLe;		 
		 Ne=(~RMate)*Pdd0*RMate;
		 Le=(~RMate)*Pdd0*RLe; 

		
		 Nr=(~RMate)*Pdd1*RMate;
		 Lr=(~RMate)*Pdd1*RLe;


		 Ne=CholeskyInv(Ne);eta0=-(~Le)*Ne*Le;
		 Le=(Ne*Le);
		 //cout<<~(DesMatPos*(Le+xt));
		 sige= eta0+~RLe*Pdd0*RLe;

		 Le=~XYZ2NEU(Le,BLH[0],BLH[1]);
		 Ne=~ diagElemMat(Rota*Ne*(~Rota));
		//Ne=1.0/(obsNum-3)*sige(0,0)*Ne;

		 Nr=CholeskyInv(Nr);eta1=-(~Lr)*Nr*Lr;
		 Lr=(Nr*Lr);
		 sigr= eta1+~RLe*Pdd1*RLe;

		 Lr=~XYZ2NEU(Lr,BLH[0],BLH[1]);
		 Nr=~  diagElemMat(Rota*Nr*(~Rota));
		 //Nr=sigr(0,0)/(obsNum-3)*Nr;

		 cnt++;
		 if (i%1000==0)
		 {
			 cout<<"DD: "<<i<<"/"<<40000<<endl;
		 }
		 //	 cout<<i<<"  "<<Le[i].Norm()<<"  "<<Lr[i].Norm() <<endl;
		 // cout<<(Le[i])<<Ne[i]<<endl;  cout<<(Lr[i])<<Nr[i]<<endl;

		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Le(0,0);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Le(0,1);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Le(0,2);

		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Ne(0,0);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Ne(0,1);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Ne(0,2);

		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Lr(0,0);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Lr(0,1);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Lr(0,2);

		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Nr(0,0);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Nr(0,1);
		 fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<Nr(0,2);
		 fout<<endl;
	 }//end i

	 fout.close();
	 int how;
 }


void Position:: PostParSingle(int numEpoch,SdData* sdData,int* satprn,DdCtrl ddctrl)
{
	int maxNum=1650,  interval=20;
	math::matrix<double>N1,N12,N2;
	math::matrix<double>U1,U2;
	math::matrix<double>Ns1,Ns12,Ns2;
	math::matrix<double>Us1,Us2,x_check;
	DdAmbInfo ambinfo;
	int i,j,k,cnt=0;

	math::matrix<double>preNorm[5];

	math::matrix<double> Rota(3,3);
	double BLH[3],XYZ[3];
	XYZ[0]=-2364333.4909;XYZ[1]=4870287.3243;XYZ[2]=-3360809.5628; 
	math::matrix<double> xt(3,1);
	xt(0,0)=-2364333.4909;xt(1,0)=4870287.3243;xt(2,0)=-3360809.5628; 
	XYZ2BLH(XYZ,1,BLH);
	double sinB=sin(BLH[0]),cosB=cos(BLH[0]);
	double sinL=sin(BLH[1]),cosL=cos(BLH[1]);
	Rota(0,0)=-sinB*cosL;	Rota(0,1)=-sinB*sinL;		Rota(0,2)=cosB;
	Rota(1,0)=-sinL;				Rota(1,1)=cosL;				Rota(1,2)=0.0;
	Rota(2,0)=cosL*cosB;		Rota(2,1)=cosB*sinL;		Rota(2,2)=sinB;

	DdObsInfo preobsinfo;
	fstream fout;
	fout.open("Par2.txt",ios::out);
	for (i=0;i<maxNum;i++)
	{
		if (maxNum==50000) break;
		int referPrn=satprn[i];
		if(sdData[i].satnum==0)  continue;
		if (i==maxNum-interval-1) break;

		j=i;

		int referPos=GetPos(sdData[j].prn,referPrn,sdData[j].satnum);
		DdData dddataCurr;	 DdObsInfo ddobsinfo;
		DoubleDiff(referPrn,referPos,sdData[i],dddataCurr);
		dddataCurr=ComObsPhsCod(ddctrl,ddobsinfo,ambinfo,dddataCurr);
		if(sdData[i].satnum-dddataCurr.pairNum!=1) continue;
		int obsNum	=ddobsinfo.SumCod()+ddobsinfo.SumPhs();//the number of all obs in one system at current epoch
		math::matrix<double>y(obsNum,1);
		math::matrix<double>V(obsNum,1);
		math::matrix<double> DesMatPos(obsNum,3);
		math::matrix<double> DesMatAmb(obsNum,obsNum);
		math::matrix<double> Weight(obsNum,obsNum);
		math::matrix<double> L(obsNum,1);
		FormDesMatPos(DesMatPos,L,dddataCurr,ddobsinfo,ddctrl);
		FormDesMatAmb(DesMatAmb,ddctrl,dddataCurr,ddobsinfo,ambinfo);
		FormWeight(ddctrl,dddataCurr,ddobsinfo);
		FormResidual(L,y,dddataCurr,ddctrl,ambinfo);
		int resultmode=-1,fixmode=2;double ratio=0.0;
		x_check = SoluShortSequence(ddctrl,DesMatPos,DesMatAmb,Weight,L,ddobsinfo,preNorm,ambinfo,preobsinfo,ddobsinfo,resultmode,ratio,fixmode);	    

		x_check=~XYZ2NEU(x_check,BLH[0],BLH[1]);
		cnt++;
		if (i%1000==0)
		{
			cout<<"DD: "<<i<<"/"<<40000<<endl;
		}
		//	 cout<<i<<"  "<<Le[i].Norm()<<"  "<<Lr[i].Norm() <<endl;
		// cout<<(Le[i])<<Ne[i]<<endl;  cout<<(Lr[i])<<Nr[i]<<endl;

		fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<x_check(0,0);
		fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<x_check(0,1);
		fout<<setiosflags(ios::fixed)<<setprecision(4)<<setw(9)<<x_check(0,2);

		//	fout<<setiosflags(ios::fixed)<<setprecision(2)<<setw(9)<<Ne(0,0);

		fout<<endl;
	}//end i

	fout.close();
	int how;
}

/***************************************************************************************************************/
/*
 * after single system processing, the design matrices and weight mat are formed.
 * this function puts two system together.
 * I:
 *	 ddctrl1				the control for system1
 *	 ddctrl2				the control for system2.
 *   dddata1			the system1 data
 *   dddata2			the system2 data
 */
void Position::DoubleSystemPosition(DdCtrl ddctrl1,DdCtrl ddctrl2,DdData dddata1,DdData dddata2,DdObsInfo obsinfo1,DdObsInfo obsinfo2)
{

}


/************************************************************************/
/*              ERTK with BDS triple frequency                                     */
/************************************************************************/

/*
 *Input:
 *			curData	the uncombined data
 *							phs	unit: cycle
 *							cod	unit: m
 *			
 * Output:
 *			ewl1  (1,4,-5)   -->P(1,1,0)      ewl unit: cycle
 *			ewl2	 (0,-1,1)   -->P(0,1,1)
 */

static double EpochCnt1[16]={0};
static double EpochCnt2[16]={0};
static double EwlHat1[16]={0};
static double EwlHat2[16]={0};

void Position::GetEWLAmbBDS(double* ewl1,double* ewl2,int* fixflag1,int* fixflag2, double threshold,DdData curData,int EpochCtrl )
{

	int sysid=Sysid(curData.refPrn);

	double P011=0.0,P110=0.0,Phi14_5=0.0,Phi0_11=0.0;
	int coef011[3]={0,1,1},coef110[3]={1,1,0},coef14_5[3]={1,4,-5},coef0_11[3]={0,-1,1};

	double lam14_5=CLIGHT/CombFreq(sysid,coef14_5);
	double lam0_11=CLIGHT/CombFreq(sysid,coef0_11);
	/*fstream fracEwl;
	fracEwl.open("FracFile.txt",ios::app);
	fracEwl<<setiosflags(ios::fixed)<<setprecision(2)<<setw(10)<<curData.sec<<setw(4)<<curData.pairNum<<endl;*/
	
	for (int i=0;i<curData.pairNum;i++)
	{

		if (curData.datarecord[i].numVadCod+curData.datarecord[i].numVadPhs>=6)
		{
			int Prn=curData.rovPrn[i]-200;

			int flag=Sum(curData.datarecord[i].isCycleSlip,3);
			if(flag) /*cycle slip*/
			{
				fixflag1[i]=0;
				EpochCnt1[Prn-1]=EpochCnt2[Prn-1]=0;
			}

			double frac,ewlInt;
			if (ewl1 && fixflag1[i]==0)
			{
				EpochCnt1[Prn-1]++;
				if (EpochCnt1[Prn-1]>10) EpochCnt1[Prn-1]=EwlHat1[Prn-1]=0.0;
				Phi14_5=CombObsCycle(sysid,coef14_5,curData.datarecord[i].Phase);
				P110	=CombObs(sysid,coef110,curData.datarecord[i].PsRange);
				
				if (EpochCnt1[Prn-1]>1.0  && fabs((P110-Phi14_5)/lam14_5-EwlHat1[Prn-1])>1.0  )
				{
					EwlHat1[Prn-1]=EpochCnt1[Prn-1]=0.0;
				}
				if(EpochCnt1[Prn-1]>0.0) 
					EwlHat1[Prn-1]=EwlHat1[Prn-1]*(1.0-1.0/EpochCnt1[Prn-1])+(P110-Phi14_5)/lam14_5/EpochCnt1[Prn-1];

				ewlInt=ROUND(EwlHat1[Prn-1]);
			//	fracEwl<<setiosflags(ios::fixed)<<setw(4)<<curData.rovPrn[i]<<setprecision(5)<<setw(8)<<ewlInt-ewl1[i];
				if((frac=fabs(ewlInt-EwlHat1[Prn-1]))<threshold&&EpochCnt1[Prn-1]>=EpochCtrl)//
				{
					ewl1[i]=ewlInt;
					if(fixflag1) fixflag1[i]=1;
					EpochCnt1[Prn-1]=0.0;
				}
				else
				{
					if(fixflag1) fixflag1[i]=0;
				}
			}
			if(flag) fixflag2[i]=0;
			if (ewl2 && fixflag2[i]==0)
			{
				EpochCnt2[Prn-1]++;		
				if (EpochCnt2[Prn-1]>10) EpochCnt2[Prn-1]=EwlHat2[Prn-1]=0.0;
				Phi0_11=CombObsCycle(sysid,coef0_11,curData.datarecord[i].Phase);
				P011	=CombObs(sysid,coef011,curData.datarecord[i].PsRange);
				EwlHat2[Prn-1]=EwlHat2[Prn-1]*(1.0-1.0/EpochCnt2[Prn-1])+(P011-Phi0_11)/lam0_11/EpochCnt2[Prn-1];
				ewlInt=ROUND(EwlHat2[Prn-1]);
			//	fracEwl<<setiosflags(ios::fixed)<<setprecision(5)<<setw(8)<<ewlInt-ewl2[i]<<endl;
				if((frac=fabs(ewlInt-EwlHat2[Prn-1]))<threshold&&EpochCnt2[Prn-1]>=EpochCtrl)//
				{
					ewl2[i]=ewlInt;
					if(fixflag2) fixflag2[i]=1;
					EpochCnt1[Prn-1]=0.0;
				}
				else
				{
					if(fixflag2) fixflag2[i]=0;
				}
			}
		}

	}
//	fracEwl.close();
}



static int ErtkModel=-1; /*0 =iono fix, 1 =iono float with EWL*/

static int ErtkInitial=0; /* 0=Initial, 1=Done*/

/*
 * Input:
 *			curData	current epoch data with combined obs unit: meter
 *			preData	previous epoch data with combined obs unit: meter
 * Output:
 *	
 *	Note: 
 *			the frequency order =B1, B2, B3
 *			code data are not combined.
 *			phase data are combined by  (1,4,-5) and(0,-1,1) 
 */
DMatrix Position::ErtkBDS(DdData curData)
{
	int numphs=curData.NoPhs(0)+curData.NoPhs(1);//suppose NoPhs(0)=NoPhs(1)
	int UnfixNum=AmbInfoSys.TotalUnfixNum(2);
	int Unfix=(UnfixNum>0)?UnfixNum:1;
	DMatrix DesPos(numphs,3),DesIono(numphs,numphs/2),L(numphs,1),DesAmb(numphs,Unfix),P(numphs,numphs);
	FormDesMatPos(DesPos,L,curData,0,2);
	FormDesMatIono(DesIono,curData,0,2,NULL,DdControl.freqPhs);

	int restAmb=0;
	restAmb=FormDesMatAmb(DesAmb,curData,2,0,DdControl.freqPhs,AmbInfoSys);
	
	FormResidual(L,curData,0,2);
	ReFormConstWithAmb(L,AmbInfoSys,0,2,DdControl.freqPhs,curData);
	P=FormWeightErtk(numphs/2,DdObsSys.eleRefBase,DdObsSys.eleRefRov,DdObsSys.eleRovBase,DdObsSys.eleRovRov);

	DMatrix N11=~DesPos*P*DesPos,U1=~DesPos*P*L,x_hat(3,1);

	ErtkModel=0;
	if (ErtkModel==0)
	{
		if(restAmb==0) x_hat=CholeskyInv(N11)*U1;
		if (restAmb==1)
		{
			DMatrix N12=~DesPos*P*DesAmb,N22=~DesAmb*P*DesAmb,U2=~DesAmb*P*L;
			N11=N11-N12*CholeskyInv(N22)*(~N12);
			U1=U1-N12*CholeskyInv(N22)*U2;
			x_hat=CholeskyInv(N11)*U1;
		}
	}

	
	if(!_finite(x_hat(0,0)))
	{
		restAmb=restAmb;
	}

	if(_finite(x_hat(0,0))&&fabs(x_hat(0,0))<1000.0)
	{
		for(int i=0;i<3;i++) x_hat(i,0)+=curData.rovRecPos[i];
		//cout<<setiosflags(ios::fixed)<<setprecision(3)<<~x_hat;
		fstream crdFile;
		crdFile.open("CrdFileFix.txt",ios::app);
		crdFile<<setiosflags(ios::fixed)<<setprecision(3)<<~x_hat;
		crdFile.close();
	}
	return x_hat;
}

bool Position::ErtkBDSFloat(DdData curData)
{
	int numPhsType=2,numCodType=0;
	int numphs=0;
	int numcod=0;
	for (int i=0;i<numPhsType;i++) numphs+=curData.NoPhs(i);
	for (int i=0;i<numCodType;i++) numcod+=curData.NoCod(i);
	
	int UnfixNum=AmbInfoSys.TotalUnfixNum(numPhsType);

	int Unfix=(UnfixNum>0)?UnfixNum:1;
	int row=numphs+numcod;
	if (UnfixNum+3+curData.NoPhs(0)>=row)
	{
		return false;
	}

	DMatrix DesPos(row,3),DesIono(row,row/(numCodType+numPhsType)),L(row,1),
				 DesAmb(row,Unfix),P(numphs,numphs);
	FormDesMatPos(DesPos,L,curData,numCodType,numPhsType);
	FormDesMatIonoBDSErtk(DesIono,curData,numCodType,numPhsType,DdControl.freqCod,DdControl.freqPhs);
	//cout<<DesIono;
	int restAmb=0;
	restAmb=FormDesMatAmb(DesAmb,curData,numPhsType,numCodType,DdControl.freqPhs,AmbInfoSys);
	FormResidual(L,curData,numCodType,numPhsType);
	ReFormConstWithAmb(L,AmbInfoSys,numcod,numPhsType,DdControl.freqPhs,curData);
	P=FormWeightErtk(numphs/2,DdObsSys.eleRefBase,DdObsSys.eleRefRov,DdObsSys.eleRovBase,DdObsSys.eleRovRov);
	if(numCodType!=0)
	{
		DMatrix Pcod=FormWeightSingleFreqPhs(numcod/numCodType,DdObsSys.eleRefBase,DdObsSys.eleRefRov,DdObsSys.eleRovBase,DdObsSys.eleRovRov);	
		Pcod/=10000.0;
		Pcod=DiagMatSym(Pcod,Pcod);
		P=DiagMatSym(Pcod,P);
	}

	DMatrix N11=~DesPos*P*DesPos,U1=~DesPos*P*L,x_hat(3,1),iota;
	ErtkModel=1;
	if (ErtkModel==1)
	{
		DMatrix N12=~DesPos*P*DesIono,N22=~DesIono*P*DesIono,U2=~DesIono*P*L;
		if(restAmb==0)SolveNormalEquationCholesky2(N11,N12,N22,U1,U2);
		if (restAmb==1)
		{
			DMatrix N13=~DesPos*P*DesAmb,N23=~DesIono*P*DesAmb,
				N33=~DesAmb*P*DesAmb,U3=~DesAmb*P*L,
				invN33=CholeskyInv(N33);
			N11=N11-N13*invN33*(~N13); 
			N12=N12-N13*invN33*(~N23);
			N22=N22-N23*invN33*(~N23);
			U1=U1-N13*invN33*U3;
			U2=U2-N23*invN33*U3;

			SolveNormalEquationCholesky2(N11,N12,N22,U1,U2);
		}
		x_hat=U1;
		iota=U2;
	}

	if(!_finite(x_hat(0,0))||fabs(x_hat(0,0))>1000.0)
	{
		cout<<"What's that?          "<<x_hat(0,0)<<endl;
		return false;
	}

	if(_finite(x_hat(0,0))&&fabs(x_hat(0,0))<1000.0)
	{
		for(int i=0;i<3;i++) x_hat(i,0)+=curData.rovRecPos[i];
		//cout<<setiosflags(ios::fixed)<<setprecision(3)<<~x_hat;
		fstream IonoFile;
		IonoFile.open("IonoFile.txt",ios::app);
		IonoFile<<setiosflags(ios::fixed)<<setw(4)<<curData.pairNum<<endl;
		for(int i=0;i<curData.pairNum;i++)
			IonoFile<<setiosflags(ios::fixed)<<setw(3)<<curData.rovPrn[i]<<
							setw(10)<<setprecision(6)<<iota(i,0)<<endl;
		IonoFile.close();

		fstream crdFile;
		crdFile.open("CrdFileFloat.txt",ios::app);
		crdFile<<setiosflags(ios::fixed)<<setprecision(3)<<~x_hat;
		crdFile.close();
	}
	return true;
}




/*
 *form weight of ERTK 1.(1,-1,0) and 2.(0,-1,1)
 *Input:
 *		numSingle   single frequency obs of phase
 */
DMatrix Position::FormWeightErtkEXT(int singleFreqObsNum,double eleRefBase,double eleRefRov,double* eleRovRov,double* eleRovBase)
{
	int coef1[3]={1,-1,0},coef2[3]={0,-1,1};
	DMatrix initMat(2,2),Qinit(singleFreqObsNum,singleFreqObsNum),P;
	double lam1=CLIGHT/CombFreq(5,coef1),lam2=CLIGHT/CombFreq(5,coef2),lam[3]={CLIGHT/FreqSys(5,0),CLIGHT/FreqSys(5,1),CLIGHT/FreqSys(5,2)};
	initMat(0,0)=SQ(lam1)*(SQ(coef1[0]/lam[0])+SQ(coef1[1]/lam[1])+SQ(coef1[2]/lam[2]));
	initMat(1,1)=SQ(lam2)*(SQ(coef2[0]/lam[0])+SQ(coef2[1]/lam[1])+SQ(coef2[2]/lam[2]));
	initMat(0,1)=initMat(1,0)=lam2*lam1*(coef1[0]*coef2[0]/SQ(lam[0])+coef1[1]*coef2[1]/SQ(lam[1])+coef1[2]*coef2[2]/SQ(lam[2]));
	for (int i=0;i<singleFreqObsNum;i++)
	{
		for(int j=i;j<singleFreqObsNum;j++)
		{
			Qinit(i,j)=cofactor(eleRefBase,1)+cofactor(eleRefRov,1);
			Qinit(j,i)=Qinit(i,j);
		}
	}
	for (int i=0;i<singleFreqObsNum;i++)
	{
		Qinit(i,i)+=cofactor(eleRovRov[i],1)+cofactor(eleRovBase[i],1);
	}
	return Kronecker(CholeskyInv(initMat),CholeskyInv(Qinit),4);
}

void FormDesMatIonoBDSErtkEXT(math::matrix<double>& DesMatIono,DdData dddata,int numCodType,int numPhsType,double* freqCod,double* freqPhs)
{
	int num=numCodType;
	int cnt=0;int cnt1=0;
	double freq1=FreqSys(Sysid(dddata.refPrn),0);
	for (int i=0;i<num;i++)
	{
		cnt1=0;
		for (int j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgCod[i]==1)
			{
				DesMatIono(cnt,cnt1)=SQ(freq1/freqCod[i]);
				cnt++;cnt1++;
			}
			else if (dddata.datarecord[j].vadFlgCod[i]==0)
			{
				cnt1++;
			}
		}
	}
	num=numPhsType;
	for (int i=0;i<num;i++)
	{
		cnt1=0;
		for (int j=0;j<dddata.pairNum;j++)
		{
			if (dddata.datarecord[j].vadFlgPhs[i]==1)
			{
				if(i==0) DesMatIono(cnt,cnt1)=-(freq1-SQ(freq1)/FREQ7_BDS)/freqPhs[i];
				if(i==1) DesMatIono(cnt,cnt1)=-(-SQ(freq1)/FREQ7_BDS+SQ(freq1)/FREQ6_BDS)/freqPhs[i];
				if(i==2) DesMatIono(cnt,cnt1)=-1.0;
				cnt++;
				cnt1++;
			}
			else if (dddata.datarecord[j].vadFlgCod[i]==0)
			{
				cnt1++;
			}

		}
	}

}

/*
 * Input:
 *			curData	current epoch data with combined obs unit: meter
 *			preData	previous epoch data with combined obs unit: meter
 * Output:
 *	
 *	Note: 
 *			the frequency order =B1, B2, B3
 *			code data are not combined.
 *			phase data are combined by 1.(1,-1,0) and 2.(0,-1,1)
 */
DMatrix Position::ErtkBDSEXT(DdData curDatas)
{
	double freqExt[3]={FREQ2_BDS-FREQ7_BDS,FREQ6_BDS-FREQ7_BDS,0};
	DdData curData=curDatas;
	for (int i=0;i<curData.pairNum;i++)
	{
		curData.datarecord[i].Phase[0]=(curData.datarecord[i].Phase[0]*CLIGHT-curData.datarecord[i].Phase[1]*CLIGHT)/freqExt[0];
		curData.datarecord[i].Phase[1]=(-curData.datarecord[i].Phase[1]*CLIGHT+curData.datarecord[i].Phase[2]*CLIGHT)/freqExt[1];
	}
	/*       */
	DdAmbInfo TmpAmbinfo=AmbInfoSys;
	
	double lam1_10=CLIGHT/(FREQ2_BDS-FREQ7_BDS);
	for (int i=0;i<curData.pairNum;i++)
	{
		if(AmbInfoSys.fixFlag[0][i]*AmbInfoSys.fixFlag[1][i]==0)
			TmpAmbinfo.fixFlag[0][i]=0;
		if(AmbInfoSys.fixFlag[0][i]*AmbInfoSys.fixFlag[1][i]==1) 
			TmpAmbinfo.fixSolu[0][i]+=5*TmpAmbinfo.fixSolu[1][i];
	}

	int numphs=curData.NoPhs(0)+curData.NoPhs(1);//suppose NoPhs(0)=NoPhs(1)
	int UnfixNum=TmpAmbinfo.TotalUnfixNum(2);
	int Unfix=(UnfixNum>0)?UnfixNum:1;
	DMatrix DesPos(numphs,3),DesIono(numphs,numphs/2),L(numphs,1),DesAmb(numphs,Unfix),P(numphs,numphs);
	FormDesMatPos(DesPos,L,curData,0,2);
	//FormDesMatIono(DesIono,curData,0,2,NULL,DdControl.freqPhs);

	int restAmb=0;
	restAmb=FormDesMatAmb(DesAmb,curData,2,0,freqExt,TmpAmbinfo);
	
	FormResidual(L,curData,0,2);
	ReFormConstWithAmb(L,TmpAmbinfo,0,2,freqExt,curData);
	P=FormWeightErtkEXT(numphs/2,DdObsSys.eleRefBase,DdObsSys.eleRefRov,DdObsSys.eleRovBase,DdObsSys.eleRovRov);

	DMatrix N11=~DesPos*P*DesPos,U1=~DesPos*P*L,x_hat(3,1);

	ErtkModel=0;
	if (ErtkModel==0)
	{
		if(restAmb==0) x_hat=CholeskyInv(N11)*U1;
		if (restAmb==1)
		{
			DMatrix N12=~DesPos*P*DesAmb,N22=~DesAmb*P*DesAmb,U2=~DesAmb*P*L;
			N11=N11-N12*CholeskyInv(N22)*(~N12);
			U1=U1-N12*CholeskyInv(N22)*U2;
			x_hat=CholeskyInv(N11)*U1;
		}
	}

	
	if(!_finite(x_hat(0,0)))
	{
		restAmb=restAmb;
	}

	if(_finite(x_hat(0,0))&&fabs(x_hat(0,0))<1000.0)
	{
		for(int i=0;i<3;i++) x_hat(i,0)+=curData.rovRecPos[i];
		//cout<<setiosflags(ios::fixed)<<setprecision(3)<<~x_hat;
		fstream crdFile;
		crdFile.open("CrdFileFix.txt",ios::app);
		crdFile<<setiosflags(ios::fixed)<<setprecision(3)<<~x_hat;
		crdFile.close();
	}
	return x_hat;
}
/*
 * Input:
 *			curData	current epoch data with combined obs unit: meter
 *	
 *	Note: 
 *			the frequency order =B1, B2, B3
 *			code data are not combined.
 *			phase data are combined by (1,-1,0)and  (0,-1,1) 
 *			(1,-1,0)=(1,4,-5)+5(0,-1,1)
 */
static DdAmbInfo TTTT;
bool Position::ErtkBDSFloatEXT(DdData curDatas)
{
	double freqExt[3]={FREQ2_BDS-FREQ7_BDS,FREQ6_BDS-FREQ7_BDS,0};
	DdData curData=curDatas;
	for (int i=0;i<curData.pairNum;i++)
	{
		curData.datarecord[i].Phase[0]=(curData.datarecord[i].Phase[0]*CLIGHT-curData.datarecord[i].Phase[1]*CLIGHT)/freqExt[0];
		curData.datarecord[i].Phase[1]=(-curData.datarecord[i].Phase[1]*CLIGHT+curData.datarecord[i].Phase[2]*CLIGHT)/freqExt[1];
	}
	
	int numPhsType=2,numCodType=0;
	int numphs=0;
	int numcod=0;
	for (int i=0;i<numPhsType;i++) numphs+=curData.NoPhs(i);
	for (int i=0;i<numCodType;i++) numcod+=curData.NoCod(i);

	DdAmbInfo TmpAmbinfo=AmbInfoSys;
	
	double lam1_10=CLIGHT/(FREQ2_BDS-FREQ7_BDS);
	for (int i=0;i<curData.pairNum;i++)
	{
		if(AmbInfoSys.fixFlag[0][i]*AmbInfoSys.fixFlag[1][i]==0)
		{
			TmpAmbinfo.fixFlag[0][i]=0; 
			TmpAmbinfo.fixSolu[0][i]=0.0;
		}
		if(AmbInfoSys.fixFlag[0][i]*AmbInfoSys.fixFlag[1][i]==1) 
			TmpAmbinfo.fixSolu[0][i]+=5*TmpAmbinfo.fixSolu[1][i];
	}

	int UnfixNum=TmpAmbinfo.TotalUnfixNum(numPhsType);

	int Unfix=(UnfixNum>0)?UnfixNum:1;
	int row=numphs+numcod;
	if (UnfixNum+3+curData.NoPhs(0)>=row)
	{
		return false;
	}

	DMatrix DesPos(row,3),DesIono(row,row/(numCodType+numPhsType)),L(row,1),
		DesAmb(row,Unfix),P(numphs,numphs);
	FormDesMatPos(DesPos,L,curData,numCodType,numPhsType);
	FormDesMatIonoBDSErtkEXT(DesIono,curData,numCodType,numPhsType,DdControl.freqCod,freqExt);
	//cout<<DesIono;
	int restAmb=0;
	restAmb=FormDesMatAmb(DesAmb,curData,numPhsType,numCodType,freqExt,TmpAmbinfo);
	DMatrix tAmb=DesIono;
	FormResidual(L,curData,numCodType,numPhsType);
	ReFormConstWithAmb(L,TmpAmbinfo,numcod,numPhsType,freqExt,curData);
	P=FormWeightErtkEXT(numphs/2,DdObsSys.eleRefBase,DdObsSys.eleRefRov,DdObsSys.eleRovBase,DdObsSys.eleRovRov);
	if(numCodType!=0)
	{
		DMatrix Pcod=FormWeightSingleFreqPhs(numcod/numCodType,DdObsSys.eleRefBase,DdObsSys.eleRefRov,DdObsSys.eleRovBase,DdObsSys.eleRovRov);	
		Pcod/=10000.0;
		Pcod=DiagMatSym(Pcod,Pcod);
		P=DiagMatSym(Pcod,P);
	}

	DMatrix N11=~DesPos*P*DesPos,U1=~DesPos*P*L,x_hat(3,1),iota;
	ErtkModel=1;
	if (ErtkModel==1)
	{
		DMatrix N12=~DesPos*P*DesIono,N22=~DesIono*P*DesIono,U2=~DesIono*P*L,XX;
		
		if(restAmb==0)SolveNormalEquationCholesky2(N11,N12,N22,U1,U2);
		if (restAmb==1)
		{
			DMatrix N13=~DesPos*P*DesAmb,N23=~DesIono*P*DesAmb,
				N33=~DesAmb*P*DesAmb,U3=~DesAmb*P*L,
				invN33=!(N33);
			N11=N11-N13*invN33*(~N13); 
			N12=N12-N13*invN33*(~N23);
			N22=N22-N23*invN33*(~N23);
			U1=U1-N13*invN33*U3;
			U2=U2-N23*invN33*U3;
			
			SolveNormalEquationCholesky2(N11,N12,N22,U1,U2);
			x_hat=U1;
			if(!_finite(x_hat(0,0))||fabs(x_hat(0,0))>1000.0)
			{
				fstream debugFile;
				debugFile.open("MATFILE.txt",ios::app);
				debugFile<<setiosflags(ios::fixed)<<setw(20)<<setprecision(15)<<DesPos<<endl;
				debugFile<<setiosflags(ios::fixed)<<setw(20)<<setprecision(15)<<DesAmb<<endl;
				debugFile<<setiosflags(ios::fixed)<<setw(20)<<setprecision(15)<<DesIono<<endl;
				debugFile<<setiosflags(ios::fixed)<<setw(20)<<setprecision(15)<<P<<endl;
				debugFile.close();
				cout<<"What's that?          "<<x_hat(0,0)<<endl;
				return false;
			}
		}
		x_hat=U1;
		iota=U2;
		
		
		//TTTT=TmpAmbinfo;
	}

	

	if(_finite(x_hat(0,0))&&fabs(x_hat(0,0))<1000.0)
	{
		for(int i=0;i<3;i++) x_hat(i,0)+=curData.rovRecPos[i];
		//cout<<setiosflags(ios::fixed)<<setprecision(3)<<~x_hat;
		fstream IonoFile;
		IonoFile.open("IonoFile.txt",ios::app);
		IonoFile<<setiosflags(ios::fixed)<<setw(4)<<curData.pairNum<<endl;
		for(int i=0;i<curData.pairNum;i++)
			IonoFile<<setiosflags(ios::fixed)<<setw(3)<<curData.rovPrn[i]<<
			setw(10)<<setprecision(6)<<iota(i,0)<<endl;
		IonoFile.close();

		fstream crdFile;
		crdFile.open("CrdFileFloat.txt",ios::app);
		crdFile<<setiosflags(ios::fixed)<<setprecision(3)<<~x_hat;
		crdFile.close();
	}
	return true;
}




static DMatrix Qaa,a;

/*phase order (EWL1,EWL2, N1) unit: m*/
void Position::ErtkBDSWithNl(DdData curData)
{
	int numPhsType=3,numCodType=3;
	int numphs=0;
	int numcod=0;
	for (int i=0;i<numPhsType;i++) numphs+=curData.NoPhs(i);
	for (int i=0;i<numCodType;i++) numcod+=curData.NoCod(i);

	int UnfixNum=AmbInfoSys.TotalUnfixNum(numPhsType);
	int Unfix=(UnfixNum>0)?UnfixNum:1;
	int row=numphs+numcod;
	DMatrix DesPos(row,3),DesIono(row,row/(numCodType+numPhsType)),L(row,1),
		DesAmb(row,Unfix),P(numphs,numphs);
	FormDesMatPos(DesPos,L,curData,numCodType,numPhsType);
	FormDesMatIonoBDSErtk(DesIono,curData,numCodType,numPhsType,DdControl.freqCod,DdControl.freqPhs);

	int restAmb=0;
	restAmb=FormDesMatAmb(DesAmb,curData,numPhsType,numcod,DdControl.freqPhs,AmbInfoSys);
	FormResidual(L,curData,numCodType,numPhsType);
	ReFormConstWithAmb(L,AmbInfoSys,numcod,numPhsType,DdControl.freqPhs,curData);
	P=FormWeightErtkNl(numphs/numPhsType,DdObsSys.eleRefBase,DdObsSys.eleRefRov,DdObsSys.eleRovBase,DdObsSys.eleRovRov);
	if(numCodType!=0)
	{
		DMatrix Pcod=FormWeightSingleFreqPhs(numcod/numCodType,DdObsSys.eleRefBase,DdObsSys.eleRefRov,DdObsSys.eleRovBase,DdObsSys.eleRovRov);	
		Pcod/=10000.0;
		DMatrix Ps=Pcod;
		if(numCodType>=2) Pcod=DiagMatSym(Ps,Ps);
		if(numCodType==3) Pcod=DiagMatSym(Pcod,Ps);
		P=DiagMatSym(Pcod,P);
	}
	DMatrix N11=~DesPos*P*DesPos,U1=~DesPos*P*L,x_hat(3,1),a_hat;
	ErtkModel=2;
	if (ErtkModel==2)
	{
		DMatrix N12=~DesPos*P*DesAmb,N22=~DesAmb*P*DesAmb,U2=~DesAmb*P*L,
			         N13=~DesPos*P*DesIono,N33=~DesIono*P*DesIono,N23=~DesAmb*P*DesIono,
					 U3=~DesIono*P*L;

		if(restAmb==0)
		{
			SolveNormalEquationCholesky2(N11,N13,N33,U1,U3);
		}
		if (restAmb==1)
		{
			DMatrix invN33=CholeskyInv(N33);
			N11=N11-N13*invN33*(~N13); 
			N12=N12-N13*invN33*(~N23);
			N22=N22-N23*invN33*(~N23);
			U1=U1-N13*invN33*U3;
			U2=U2-N23*invN33*U3;

			SolveNormalEquationCholesky2(N11,N12,N22,U1,U2);
		}
		x_hat=U1;
		a_hat=U2;
		if(epoch==0) 
		{
			if(UnfixNum>curData.pairNum)
			{
				/*get the vc-mat and float solution of B1 */
				Qaa=GetBlockMat(N22,UnfixNum-curData.pairNum+1,UnfixNum,UnfixNum-curData.pairNum+1,UnfixNum,1);
				a=GetBlockMat(a_hat,UnfixNum-curData.pairNum+1,UnfixNum,1,1,2);
			}
			if(UnfixNum==curData.pairNum)
			{
				Qaa=N22;
				a=a_hat;
			}
				
		}
		else
		{
			DMatrix Qtaa,at_hat;
			if(UnfixNum>curData.pairNum)
			{
				Qtaa=GetBlockMat(N22,UnfixNum-curData.pairNum+1,UnfixNum,UnfixNum-curData.pairNum+1,UnfixNum,1);
				at_hat=GetBlockMat(a_hat,UnfixNum-curData.pairNum+1,UnfixNum,1,1,2);
			}
			if(UnfixNum==curData.pairNum)
			{
				Qtaa=N22;
				at_hat=a_hat;
			}
			if (UnfixNum>=3)
			{
				/*a=a+Qaa*CholeskyInv(Qaa+Qtaa)*(at_hat-a);
				Qaa=Qaa-Qaa*CholeskyInv(Qaa+Qtaa)*Qaa;*/
				a=at_hat;Qaa=Qtaa;
			}

		}
		double ratio=0.0;
		if (UnfixNum>=5)
		{
			Ambiguity ar;
			double s[2]={0,0};DMatrix F(a.RowNo(),2);
			ar.Lambda(a.RowNo(),2,a,Qaa,F,s);
			ratio=s[1]/s[0];
			if(ratio>1.5) 
			{
				for (int i=0;i<curData.pairNum;i++) 
				{
					AmbInfoSys.fixFlag[2][i]=1;
					AmbInfoSys.fixSolu[2][i]=F(i,0);
					AmbInfoSys.prnList[2][i]=curData.rovPrn[i];
				}
			} 
				
			cout<<"  ratio   "<<ratio<<"   ";
		}
		

	}

	if(!_finite(x_hat(0,0))||fabs(x_hat(0,0))>1000.0)
	{
		cout<<fabs(x_hat(0,0))<<endl;
	}
	if(_finite(x_hat(0,0))&&fabs(x_hat(0,0))<1000.0)
	{

		for(int i=0;i<3;i++) x_hat(i,0)+=curData.rovRecPos[i];
		//cout<<setiosflags(ios::fixed)<<setprecision(3)<<~x_hat;
		

		fstream crdFile;
		crdFile.open("CrdFileFloatNL.txt",ios::app);
		crdFile<<setiosflags(ios::fixed)<<setprecision(3)<<~x_hat;
		crdFile.close();
	}
	epoch++;
}


const int NumSatBDS=16;
const int WindowMax=60;
static int epochCnt[NumSatBDS]={0};
static double EwlCheckObs1[NumSatBDS][WindowMax]={0.0};
static double EwlCheckObs2[NumSatBDS][WindowMax]={0.0};
static double LcB1B2[NumSatBDS][WindowMax]={0.0};

/* smooth ewl obs with lc 
 *	I:
 *		curdata phase uint: meter
 **/
void Position::IonoSmooth(DdData& curdata,DdData curdataCycle)
{
	fstream IonoFile;
	IonoFile.open("IonoFile.txt",ios::app);
	IonoFile<<setiosflags(ios::fixed)<<setw(3)<<curdata.pairNum<<endl;

	double K=(SQ(FREQ2_BDS)-SQ(FREQ7_BDS));
	for (int i=0;i<curdata.pairNum;i++)
	{
		int pos=FindPosInt(AmbInfoSys.prnList[0],curdata.pairNum,curdata.rovPrn[i]);
		int prn=curdata.rovPrn[i]-200;
		if (curdata.datarecord[i].isCycleSlip[0]==1)
		{
			epochCnt[prn-1]=0;
			for(int j=0;j<WindowMax;j++)EwlCheckObs1[prn-1][j]=EwlCheckObs2[prn-1][j]=LcB1B2[prn-1][j]=0.0;
		}
		
		if (AmbInfoSys.fixFlag[0][pos]*AmbInfoSys.fixFlag[1][pos]==1)
		{
			double beta1=BETA_BDS1,beta2=BETA_BDS2;
			double check_ewl1=curdata.datarecord[i].Phase[0]+AmbInfoSys.fixSolu[0][pos]*(CLIGHT/FREQ_EWL_BDS1);
			double check_ewl2=curdata.datarecord[i].Phase[1]+AmbInfoSys.fixSolu[1][pos]*(CLIGHT/FREQ_EWL_BDS2);
			double dIono=(check_ewl2-check_ewl1)/(beta1-beta2);
			
			check_ewl1+=beta1*dIono;
			check_ewl2+=beta2*dIono;
			double lc=(curdataCycle.datarecord[i].Phase[0]*CLIGHT*FREQ2_BDS-curdataCycle.datarecord[i].Phase[1]*CLIGHT*FREQ7_BDS)/K;
			if (epochCnt[prn-1]==WindowMax)
			{
				ReSetPtrWithElem(EwlCheckObs1[prn-1],check_ewl1,WindowMax);
				ReSetPtrWithElem(EwlCheckObs2[prn-1],check_ewl2,WindowMax);
				ReSetPtrWithElem(LcB1B2[prn-1],lc,WindowMax);
			}
			else
			{
				EwlCheckObs1[prn-1][epochCnt[prn-1]]=check_ewl1;
				EwlCheckObs2[prn-1][epochCnt[prn-1]]=check_ewl2;
				LcB1B2[prn-1][epochCnt[prn-1]]=lc;
				epochCnt[prn-1]++;
			}
			double num=(double)epochCnt[prn-1];
			double s=(lc-Mean(LcB1B2[prn-1],epochCnt[prn-1]-1));
			IonoFile<<setiosflags(ios::fixed)<<setw(2)<<prn<<setiosflags(ios::fixed)<<setw(9)<<setprecision(5)<<dIono<<endl;
			
			double Smth1=Mean(EwlCheckObs1[prn-1],epochCnt[prn-1])+(num-1.0)/num*s-AmbInfoSys.fixSolu[0][pos]*(CLIGHT/FREQ_EWL_BDS1);	
			double Smth2=	Mean(EwlCheckObs2[prn-1],epochCnt[prn-1])+(num-1.0)/num*s-AmbInfoSys.fixSolu[1][pos]*(CLIGHT/FREQ_EWL_BDS2);
			curdata.datarecord[i].Phase[0]=Smth1;
			curdata.datarecord[i].Phase[1]=Smth2;
		}
		else
		{
			IonoFile<<setiosflags(ios::fixed)<<setw(2)<<prn<<setiosflags(ios::fixed)<<setw(9)<<setprecision(5)<<0.0<<endl;

		}
		
	}
	IonoFile.close();
}


/*smooth ionosphere with (B1-B2)*/
void Position::IonoSmoothB1B2(DdData& curdata,DdData curdataCycle)
{
	double K=(SQ(FREQ2_BDS)-SQ(FREQ7_BDS));
	fstream IonoFile;

	IonoFile.open("IonoFile.txt",ios::app);
	IonoFile<<setiosflags(ios::fixed)<<setw(3)<<curdata.pairNum<<endl;
	for (int i=0;i<curdata.pairNum;i++)
	{
		int pos=FindPosInt(AmbInfoSys.prnList[0],curdata.pairNum,curdata.rovPrn[i]);
		int prn=curdata.rovPrn[i]-200;
		if (curdata.datarecord[i].isCycleSlip[0]==1)
		{
			epochCnt[prn-1]=0;
			for(int j=0;j<WindowMax;j++)EwlCheckObs1[prn-1][j]=EwlCheckObs2[prn-1][j]=LcB1B2[prn-1][j]=0.0;
		}

		if (AmbInfoSys.fixFlag[0][pos]*AmbInfoSys.fixFlag[1][pos]==1)
		{
			double beta1=BETA_BDS1,beta2=BETA_BDS2;
			double check_ewl1=curdata.datarecord[i].Phase[0]+AmbInfoSys.fixSolu[0][pos]*(CLIGHT/FREQ_EWL_BDS1);
			double check_ewl2=curdata.datarecord[i].Phase[1]+AmbInfoSys.fixSolu[1][pos]*(CLIGHT/FREQ_EWL_BDS2);
			double dIono=(check_ewl2-check_ewl1)/(beta1-beta2);
			double bar_Iono=SQ(FREQ7_BDS)/K*(curdataCycle.datarecord[i].Phase[0]*CLIGHT/FREQ2_BDS-curdataCycle.datarecord[i].Phase[1]*CLIGHT/FREQ7_BDS);
			if (epochCnt[prn-1]==WindowMax)
			{
				ReSetPtrWithElem(EwlCheckObs1[prn-1],dIono,WindowMax);
				ReSetPtrWithElem(LcB1B2[prn-1],bar_Iono,WindowMax);
			}
			else
			{
				EwlCheckObs1[prn-1][epochCnt[prn-1]]=dIono;
				LcB1B2[prn-1][epochCnt[prn-1]]=bar_Iono;
				epochCnt[prn-1]++;
			}
			double num=(double)epochCnt[prn-1];
			double s=(bar_Iono-Mean(LcB1B2[prn-1],epochCnt[prn-1]-1));
			
			double Smth1=	Mean(EwlCheckObs1[prn-1],epochCnt[prn-1])+(num-1.0)/num*s;
			IonoFile<<setiosflags(ios::fixed)<<setw(2)<<prn<<setiosflags(ios::fixed)<<setw(9)<<setprecision(5)<<Smth1<<endl;
			curdata.datarecord[i].Phase[0]+=Smth1*beta1;
			curdata.datarecord[i].Phase[1]+=Smth1*beta2;
		}
		else
		{
			IonoFile<<setiosflags(ios::fixed)<<setw(2)<<prn<<setiosflags(ios::fixed)<<setw(9)<<setprecision(5)<<0.0<<endl;

		}

	}
	IonoFile.close();
}


/*for single frequency*/

static void SateDown(DdData curdata,DdData predata,DMatrix& Naa,DMatrix& Uaa)
{
	int prePhs=0,leftPhs=0;
	int preList[MAXNUMSATE];/*for the cycle slip and down*/
	int leftList[MAXNUMSATE];/*no cycle slip and no down*/
	
	for (int i=predata.pairNum;i>0;i--)
	{
		int pos=FindPosInt(curdata.rovPrn,curdata.pairNum,predata.rovPrn[i-1]);
		/*cycle slip only*/
		if((pos>-1 &&curdata.datarecord[pos].isCycleSlip[0]==1))preList[prePhs++]=predata.rovPrn[i-1];

		if (pos==-1||(pos>-1 &&curdata.datarecord[pos].isCycleSlip[0]==1))
		{
			fstream logfile;
			logfile.open("logFile.txt",ios::app);
			Naa=ElimRowColNEQ(Naa,Uaa,i);
			logfile<<setw(10)<<epoch<<"  "<<endl;
			if(pos>-1) 
			{
				cout<<"Eliminate "<<predata.rovPrn[i-1]<<"        Cycle slip!"<<endl;
				logfile<<"Eliminate "<<predata.rovPrn[i-1]<<"        Cycle slip!"<<endl;
			}
				
			if(pos==-1)
			{
				cout<<"Eliminate "<<predata.rovPrn[i-1]<<"        FALL!"<<endl;
				logfile<<"Eliminate "<<predata.rovPrn[i-1]<<"        FALL!"<<endl;
			}
			logfile.close();	
		}
		else
		{
			leftList[leftPhs++]=predata.rovPrn[i-1];
		}
	}

	/*for cycle slip only, recover the Naa's size*/
	for (int i=prePhs;i>0;i--)
	{
		int pos=FindPosInt(leftList,leftPhs,preList[i-1]);
		if (pos==-1)
		{
			Naa=InsertZeroRowCol(Naa,prePhs-i+1);
			Uaa=InsertZeroRow(Uaa,prePhs-i,1);
		}
	}

}

static void SateUp(DdData curdata,DdData predata,DMatrix& Naa,DMatrix& Uaa)
{
	
	//int preList[MAXNUMSATE],leftList[MAXNUMSATE];
	for (int i=curdata.pairNum;i>0;i--)
	{
		int pos=FindPosInt(predata.rovPrn,predata.pairNum,curdata.rovPrn[i-1]);
		if (pos==-1)
		{
			fstream logfile;
			logfile.open("logFile.txt",ios::app);
			logfile<<setw(10)<<epoch<<"  "<<endl;
			Naa=InsertZeroRowCol(Naa,i);
			Uaa=InsertZeroRow(Uaa,i-1,1);
			cout<<"Resize   "<<curdata.rovPrn[i-1]<<"        RISE!"<<endl;
			logfile<<"Resize   "<<curdata.rovPrn[i-1]<<"        RISE!"<<endl;
			logfile.close();
		}
	}
	
}



static DMatrix Naa,Ua;
/*phase order (EWL1,EWL2, N1) unit: m*/
DMatrix Position::ErtkBDSWithNlSmooth(DdData curData,DdData preData)
{
	int numPhsType=3,numCodType=0;
	int numphs=0;
	int numcod=0;
	for (int i=0;i<numPhsType;i++) numphs+=curData.NoPhs(i);
	for (int i=0;i<numCodType;i++) numcod+=curData.NoCod(i);

	int UnfixNum=AmbInfoSys.TotalUnfixNum(numPhsType);
	int Unfix=(UnfixNum>0)?UnfixNum:1;
	int row=numphs+numcod;
	DMatrix DesPos(row,3),DesIono(row,row/(numCodType+numPhsType)),L(row,1),
		DesAmb(row,Unfix),P(numphs,numphs);
	FormDesMatPos(DesPos,L,curData,numCodType,numPhsType);
	FormDesMatIonoBDSErtk(DesIono,curData,numCodType,numPhsType,DdControl.freqCod,DdControl.freqPhs);

	int restAmb=0;
	restAmb=FormDesMatAmb(DesAmb,curData,numPhsType,numcod,DdControl.freqPhs,AmbInfoSys);
	FormResidual(L,curData,numCodType,numPhsType);
	ReFormConstWithAmb(L,AmbInfoSys,numcod,numPhsType,DdControl.freqPhs,curData);
	P=FormWeightErtkNl(numphs/numPhsType,DdObsSys.eleRefBase,DdObsSys.eleRefRov,DdObsSys.eleRovBase,DdObsSys.eleRovRov);
	if(numCodType!=0)
	{
		DMatrix Pcod=FormWeightSingleFreqPhs(numcod/numCodType,DdObsSys.eleRefBase,DdObsSys.eleRefRov,DdObsSys.eleRovBase,DdObsSys.eleRovRov);	
		Pcod/=10000.0;
		DMatrix Ps=Pcod;
		if(numCodType>=2) Pcod=DiagMatSym(Ps,Ps);
		if(numCodType==3) Pcod=DiagMatSym(Pcod,Ps);
		P=DiagMatSym(Pcod,P);
	}
	DMatrix N11=~DesPos*P*DesPos,U1=~DesPos*P*L,x_hat(3,1),a_hat;
	ErtkModel=2;

		DMatrix N12=~DesPos*P*DesAmb,N22=~DesAmb*P*DesAmb,U2=~DesAmb*P*L,
			         N13=~DesPos*P*DesIono,N33=~DesIono*P*DesIono,N23=~DesAmb*P*DesIono,
					 U3=~DesIono*P*L;

		if(restAmb==0)
		{
			SolveNormalEquationCholesky2(N11,N13,N33,U1,U3);
		}
		if (restAmb==1)
		{
			DMatrix invN33=CholeskyInv(N33);
			N11=N11-N13*invN33*(~N13); 
			N12=N12-N13*invN33*(~N23);
			N22=N22-N23*invN33*(~N23);
			U1=U1-N13*invN33*U3;
			U2=U2-N23*invN33*U3;
		}


		if (epoch>0)
		{
			
			SateDown(curData,preData,Naa,Ua);
			SateUp(curData,preData,Naa,Ua);
			/*superposition the normal equation*/
			int npos=UnfixNum-curData.pairNum;
			if (npos==0)
			{
				N22+=Naa;
				U2+=Ua;
			}
			for (int i=curData.pairNum;i>0&&npos>0;i--)
			{
				U2(npos+i-1,0)+=Ua(i-1,0);
				for (int j=i;j>0;j--)
				{
					N22(npos+i-1,npos+j-1)+=Naa(i-1,j-1);
					N22(npos+j-1,npos+i-1)=N22(npos+i-1,npos+j-1);
				}
			}
		}
		if(epoch>=0) 
		{
			if(UnfixNum>curData.pairNum)
			{
				/*get the Normal equation of B1 */
				Naa=GetBlockMat(N22-~N12*CholeskyInv(N11)*N12,UnfixNum-curData.pairNum+1,UnfixNum,UnfixNum-curData.pairNum+1,UnfixNum,1);
				Ua=GetBlockMat(U2-~N12*CholeskyInv(N11)*U1,UnfixNum-curData.pairNum+1,UnfixNum,1,1,2);
			}	
			else
			{
				Naa=N22-~N12*CholeskyInv(N11)*N12;
				Ua=U2-~N12*CholeskyInv(N11)*U1;
			}
		}
		SolveNormalEquationCholesky2(N11,N12,N22,U1,U2);

		Ambiguity ar;
		double s[2]={0,0};
		DMatrix F(U2.RowNo(),2);
		ar.Lambda(U2.RowNo(),2,U2,N22,F,s);
		double ratio=s[1]/s[0];
		fstream ratioFile;
		ratioFile.open("ratioFile.txt",ios::app|ios::out);
		ratioFile<<ratio<<endl;
		ratioFile.close();
		x_hat=U1;
		a_hat=U2;
	

	if(!_finite(x_hat(0,0))||fabs(x_hat(0,0))>1000.0)
	{
		cout<<fabs(x_hat(0,0))<<endl;
	}
	if(_finite(x_hat(0,0))&&fabs(x_hat(0,0))<1000.0)
	{

		for(int i=0;i<3;i++) x_hat(i,0)+=curData.rovRecPos[i];
		//cout<<setiosflags(ios::fixed)<<setprecision(3)<<~x_hat;
		

		fstream crdFile;
		crdFile.open("CrdFileFloatNL.txt",ios::app);
		crdFile<<setiosflags(ios::fixed)<<setprecision(3)<<~x_hat;
		crdFile.close();
	}
	epoch++;
	return x_hat;
}

