/* devCommonGpib.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* gpibCore is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*
 * Current Author: Marty Kraimer
 * Original Authors: John Winans and Benjamin Franksen
*/

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include <epicsAssert.h>
#include <epicsStdio.h>
#include <recGbl.h>
#include <dbAccess.h>
#include <alarm.h>
#include <devSup.h>
#include <recSup.h>
#include <callback.h>
#include <drvSup.h>
#include <link.h>
#include <errlog.h>
#include <menuFtype.h>
#include <shareLib.h>

#include <asynDriver.h>
#include <asynGpibDriver.h>

#include "devSupportGpib.h"
#define epicsExportSharedSymbols
#include "devCommonGpib.h"



static void aiGpibFinish(gpibDpvt *pgpibDpvt,int failure);
long epicsShareAPI devGpib_initAi(aiRecord * pai)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    DEVSUPFUN  got_special_linconv = ((gDset *) pai->dset)->funPtr[5];

    result = pdevSupportGpib->initRecord((dbCommon *) pai, &pai->inp);
    if(result) return (result);
    pgpibDpvt = gpibDpvtGet(pai);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT))) {
        printf("%s invalid command type for AI record in param %d\n",
            pai->name, pgpibDpvt->parm);
        pai->pact = TRUE;
        return (S_db_badField);
    }
    if(got_special_linconv) (*got_special_linconv)(pai,TRUE);
    return (0);
}

long epicsShareAPI devGpib_readAi(aiRecord * pai)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pai);
    int cmdType;
    DEVSUPFUN got_special_linconv = ((gDset *) pai->dset)->funPtr[5];
 
    if(pai->pact) return(got_special_linconv ? 0 : 2);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    pdevSupportGpib->queueReadRequest(pgpibDpvt,aiGpibFinish);
    return(got_special_linconv ? 0 : 2);
}

static void aiGpibFinish(gpibDpvt * pgpibDpvt,int failure)
{
    double value;
    long rawvalue;
    aiRecord *pai = ((aiRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    DEVSUPFUN got_special_linconv = ((gDset *) pai->dset)->funPtr[5];

    if(failure) {; /*do nothing*/
    } else if (pgpibCmd->convert) {
        failure = pgpibCmd->convert(pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
    } else if (!pgpibDpvt->msg || ! pgpibCmd->format) {
        printf("%s Either no msg buffer or no format\n",pai->name);
        failure = -1;
    } else {/* interpret msg with predefined format and write into val/rval */
        int result = 0;
        if(got_special_linconv) {
            result = sscanf(pgpibDpvt->msg, pgpibCmd->format, &rawvalue);
            if(result==1) {pai->rval = rawvalue; pai->udf = FALSE;}
        } else {
            result = sscanf(pgpibDpvt->msg, pgpibCmd->format, &value);
            if(result==1) {pai->val = value; pai->udf = FALSE;}
        }
        if(result!=1) failure = -1;
    }
    if(failure==-1) recGblSetSevr(pai, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void aoGpibFinish(gpibDpvt *pgpibDpvt,int failure);
long epicsShareAPI devGpib_initAo(aoRecord * pao)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    DEVSUPFUN  got_special_linconv = ((gDset *) pao->dset)->funPtr[5];

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pao, &pao->out);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pao);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBSOFT))) {
        printf("%s invalid command type for AO record in param %d\n",
            pao->name, pgpibDpvt->parm);
        pao->pact = TRUE;
        return (S_db_badField);
    }
    if(got_special_linconv) (*got_special_linconv)(pao,TRUE);
    return(got_special_linconv ? 0 : 2);
}

long epicsShareAPI devGpib_writeAo(aoRecord * pao)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pao);
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int failure = 0;
    DEVSUPFUN  got_special_linconv = ((gDset *) pao->dset)->funPtr[5];
 
    if(pao->pact) return(0);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    if(!pgpibCmd->convert) {
        if (pgpibCmd->type&GPIBWRITE) {/* only if needs formatting */
            if (got_special_linconv) {
                failure = pdevSupportGpib->writeMsgLong(pgpibDpvt,pao->rval);
            } else {
                failure = pdevSupportGpib->writeMsgDouble(pgpibDpvt,pao->oval);
            }
        }
    }
    if(failure==-1) recGblSetSevr(pao, WRITE_ALARM, INVALID_ALARM);
    if(!failure) pdevSupportGpib->queueWriteRequest(pgpibDpvt,aoGpibFinish);
    return(0);
}

static void aoGpibFinish(gpibDpvt * pgpibDpvt,int failure)
{
    dbCommon *precord = pgpibDpvt->precord;

    if(failure) recGblSetSevr(precord, WRITE_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void biGpibFinish(gpibDpvt *pgpibDpvt,int failure);
long epicsShareAPI devGpib_initBi(biRecord * pbi)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    devGpibNames *pdevGpibNames;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pbi, &pbi->inp);
    if(result) return (result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pbi);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT|GPIBEFASTI|GPIBEFASTIW))) {
        printf("%s invalid command type for BI record in param %d\n",
            pbi->name, pgpibDpvt->parm);
        pbi->pact = TRUE;
        return (S_db_badField);
    }
    pdevGpibNames = devGpibNamesGet(pgpibDpvt);
    if(pdevGpibNames) {
        if (pbi->znam[0] == 0) strcpy(pbi->znam, pdevGpibNames->item[0]);
        if (pbi->onam[0] == 0) strcpy(pbi->onam, pdevGpibNames->item[1]);
    }
    return (0);
}

long epicsShareAPI devGpib_readBi(biRecord * pbi)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pbi);
    int cmdType;
 
    if(pbi->pact) return(0);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    pdevSupportGpib->queueReadRequest(pgpibDpvt,biGpibFinish);
    return(0);
}

static void biGpibFinish(gpibDpvt * pgpibDpvt,int failure)
{
    biRecord *pbi = ((biRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    devGpibParmBlock *pdevGpibParmBlock = pgpibDpvt->pdevGpibParmBlock;
    unsigned long value;

    if(failure) {; /*do nothing*/
    } else if (pgpibCmd->convert) {
        failure = pgpibCmd->convert(pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
    } else if (!pgpibDpvt->msg || ! pgpibCmd->format) {
        printf("%s Either no msg buffer or no format\n",pbi->name);
        failure = -1;
    } else {
        if(pgpibCmd->type&(GPIBEFASTI|GPIBEFASTIW)) {
            if(pgpibDpvt->efastVal>0) {
                pbi->rval = pgpibDpvt->efastVal;
            } else {
                failure = -1;
            }
        } else {
            if(sscanf(pgpibDpvt->msg, pgpibCmd->format, &value) == 1) {
                pbi->rval = value;
            } else {
                /* sscanf did not find or assign the parameter*/
                failure = -1;
                if (*pdevGpibParmBlock->debugFlag) {
                        printf("%s can't convert msg >%s<\n",
                            pbi->name, pgpibDpvt->msg);
                }
            }
        }
    }
    if(failure==-1) recGblSetSevr(pbi, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void boGpibFinish(gpibDpvt *pgpibDpvt,int failure);
static long writeBoSpecial(boRecord * pbo);
static void boGpibWorkSpecial(gpibDpvt *pgpibDpvt,int failure);
static char *ifcName[] = {"noop", "IFC", 0};
static char *renName[] = {"drop REN", "assert REN", 0};
static char *dclName[] = {"noop", "DCL", "0"};
static char *lloName[] = {"noop", "LLO", "0"};
static char *sdcName[] = {"noop", "SDC", "0"};
static char *gtlName[] = {"noop", "GTL", "0"};
static char *resetName[] = {"noop", "resetLink", "0"};

long epicsShareAPI devGpib_initBo(boRecord * pbo)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    devGpibNames *pdevGpibNames;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pbo, &pbo->out);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pbo);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&(GPIBIFC|GPIBREN|GPIBDCL|GPIBLLO|GPIBSDC|GPIBGTL|GPIBRESETLNK)) {
        /* supply defaults for menus */
        char **papname = 0;
        switch (cmdType) {
        case GPIBIFC: papname = ifcName; break;
        case GPIBREN: papname = renName; break;
        case GPIBDCL: papname = dclName; break;
        case GPIBLLO: papname = lloName; break;
        case GPIBSDC: papname = sdcName; break;
        case GPIBGTL: papname = gtlName; break;
        case GPIBRESETLNK: papname = resetName; break;
        default:
            printf("%s devGpib_initBo logic error\n",pbo->name);
        }
        if (papname) {
            if (pbo->znam[0] == 0) strcpy(pbo->znam, papname[0]);
            if (pbo->onam[0] == 0) strcpy(pbo->onam, papname[1]);
        }
    } else if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBEFASTO|GPIBSOFT))) {
        printf("%s invalid command type for BO record in param %d\n",
            pbo->name, pgpibDpvt->parm);
        pbo->pact = TRUE;
        return (S_db_badField);
    }
    pdevGpibNames = devGpibNamesGet(pgpibDpvt);
    if(pdevGpibNames) {
        if (pbo->znam[0] == 0) strcpy(pbo->znam, pdevGpibNames->item[0]);
        if (pbo->onam[0] == 0) strcpy(pbo->onam, pdevGpibNames->item[1]);
    }
    return (2);
}

long epicsShareAPI devGpib_writeBo(boRecord * pbo)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pbo);
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int failure = 0;
 
    if(pbo->pact) return(0);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    if(cmdType&(GPIBIFC|GPIBREN|GPIBDCL|GPIBLLO|GPIBSDC|GPIBGTL|GPIBRESETLNK)) 
        return(writeBoSpecial(pbo));
    if(!pgpibCmd->convert) {
        if (pgpibCmd->type&GPIBWRITE) {/* only if needs formatting */
            failure = pdevSupportGpib->writeMsgULong(pgpibDpvt,pbo->rval);
        } else if (pgpibCmd->type&GPIBEFASTO) {
            pgpibDpvt->efastVal = pbo->val;
        }
    }
    if(failure==-1) recGblSetSevr(pbo, WRITE_ALARM, INVALID_ALARM);
    if(!failure) pdevSupportGpib->queueWriteRequest(pgpibDpvt,boGpibFinish);
    return(0);
}

static void boGpibFinish(gpibDpvt * pgpibDpvt,int failure)
{
    requestProcessCallback(pgpibDpvt);
}

static long writeBoSpecial(boRecord * pbo)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pbo);
    int cmdType = gpibCmdGetType(pgpibDpvt);
 
    if(pbo->pact) return(0);
    if(cmdType&(GPIBIFC|GPIBDCL|GPIBLLO|GPIBSDC|GPIBGTL|GPIBRESETLNK)) {
        if(pbo->val==0) return(0);
    }
    pdevSupportGpib->queueRequest(pgpibDpvt,boGpibWorkSpecial);
    return(0);
}

static void boGpibWorkSpecial(gpibDpvt *pgpibDpvt,int failure)
{
    boRecord *precord = (boRecord *)pgpibDpvt->precord;
    int val = (int)precord->val;
    int cmdType = gpibCmdGetType(pgpibDpvt);
    asynGpib *pasynGpib = pgpibDpvt->pasynGpib;
    void *drvPvt = pgpibDpvt->asynGpibPvt;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    asynStatus status = asynSuccess;

    if(failure) {; /*do nothing*/
    } else if(gpibCmdTypeNoEOS(cmdType) == GPIBRESETLNK) {
        asynCommon *pasynCommon = pgpibDpvt->pasynCommon;
        void *asynCommonPvt = pgpibDpvt->asynCommonPvt;

        assert(pasynCommon);
        status = pgpibDpvt->pasynCommon->disconnect(asynCommonPvt,pasynUser);
        if(status==asynSuccess)
            status = pgpibDpvt->pasynCommon->connect(asynCommonPvt,pasynUser);
    } else if(!pasynGpib) {
        failure = -1;
        printf("%s pasynGpib is 0\n",precord->name);
    } else switch(gpibCmdTypeNoEOS(cmdType)) {
        case GPIBIFC: status = pasynGpib->ifc(drvPvt,pasynUser); break;
        case GPIBREN: status = pasynGpib->ren(drvPvt,pasynUser,val); break;
        case GPIBDCL:
            status = pasynGpib->universalCmd(drvPvt,pasynUser,IBDCL);
            break;
        case GPIBLLO:
            status = pasynGpib->universalCmd(drvPvt,pasynUser,IBLLO);
            break;
        case GPIBSDC:
            status = pasynGpib->addressedCmd(drvPvt,pasynUser,IBSDC,1);
            break;
        case GPIBGTL:
            status = pasynGpib->addressedCmd(drvPvt,pasynUser,IBGTL,1);
            break;
        default: status = asynError; break;
    }
    if(status!=asynSuccess) failure = -1;
    if(failure==-1) recGblSetSevr(precord, WRITE_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void evGpibFinish(gpibDpvt *pgpibDpvt,int failure);
long epicsShareAPI devGpib_initEv(eventRecord * pev)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pev, &pev->inp);
    if(result) return (result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pev);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT))) {
        printf("%s invalid command type for EV record in param %d\n",
            pev->name, pgpibDpvt->parm);
        pev->pact = TRUE;
        return (S_db_badField);
    }
    return (0);
}

long epicsShareAPI devGpib_readEv(eventRecord * pev)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pev);
    int cmdType;
 
    if(pev->pact) return(0);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    pdevSupportGpib->queueReadRequest(pgpibDpvt,evGpibFinish);
    return(0);
}

static void evGpibFinish(gpibDpvt * pgpibDpvt,int failure)
{
    unsigned short value;
    eventRecord *pev = ((eventRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);

    if(failure) {; /*do nothing*/
    } else if (pgpibCmd->convert) {
        failure = pgpibCmd->convert(pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
    } else if (!pgpibDpvt->msg || ! pgpibCmd->format) {
        printf("%s Either no msg buffer or no format\n",pev->name);
        failure = -1;
    } else {/* interpret msg with predefined format and write into val/rval */
        if (sscanf(pgpibDpvt->msg, pgpibCmd->format, &value) == 1) {
            pev->val = value;
            pev->udf = FALSE;
        } else { /* sscanf did not find or assign the parameter */
            failure = -1;
        }
    }
    if(failure==-1) recGblSetSevr(pev, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void liGpibFinish(gpibDpvt *pgpibDpvt,int failure);
static void liSrqHandler(void *userPrivate,int gpibAddr,int statusByte)
{
    longinRecord *pli = (longinRecord *)userPrivate;
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pli);
    pli->val = statusByte;
    pli->udf = FALSE;
    requestProcessCallback(pgpibDpvt);
}

long epicsShareAPI devGpib_initLi(longinRecord * pli)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pli, &pli->inp);
    if(result) return (result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pli);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT|GPIBSRQHANDLER))) {
        printf("%s invalid command type for LI record in param %d\n",
            pli->name, pgpibDpvt->parm);
        pli->pact = TRUE;
        return (S_db_badField);
    }
    if(cmdType&GPIBSRQHANDLER) {
        pdevSupportGpib->registerSrqHandler(pgpibDpvt,liSrqHandler,pli);
    }
    return (0);
}

long epicsShareAPI devGpib_readLi(longinRecord * pli)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pli);
    int cmdType;
 
    if(pli->pact) return(0);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSRQHANDLER)  return(0);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    pdevSupportGpib->queueReadRequest(pgpibDpvt,liGpibFinish);
    return(0);
}

static void liGpibFinish(gpibDpvt * pgpibDpvt,int failure)
{
    long value;
    longinRecord *pli = ((longinRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);

    if(failure) {; /*do nothing*/
    } else if (pgpibCmd->convert) {
        failure = pgpibCmd->convert(pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
    } else if (!pgpibDpvt->msg || ! pgpibCmd->format) {
        printf("%s Either no msg buffer or no format\n",pli->name);
        failure = -1;
    } else {/* interpret msg with predefined format and write into val/rval */
        if (sscanf(pgpibDpvt->msg, pgpibCmd->format, &value) == 1) {
            pli->val = value; pli->udf = FALSE;
        } else { /* sscanf did not find or assign the parameter */
            failure = -1;
        }
    }
    if(failure==-1) recGblSetSevr(pli, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void loGpibFinish(gpibDpvt *pgpibDpvt,int failure);
long epicsShareAPI devGpib_initLo(longoutRecord * plo)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) plo, &plo->out);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(plo);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBSOFT))) {
        printf("%s invalid command type for LO record in param %d\n",
            plo->name, pgpibDpvt->parm);
        plo->pact = TRUE;
        return (S_db_badField);
    }
    return (0);
}

long epicsShareAPI devGpib_writeLo(longoutRecord * plo)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(plo);
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int failure = 0;
 
    if(plo->pact) return(0);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    if(!pgpibCmd->convert) {
        if (pgpibCmd->type&GPIBWRITE) {/* only if needs formatting */
            failure = pdevSupportGpib->writeMsgLong(pgpibDpvt, plo->val);
        }
    }
    if(failure==-1) recGblSetSevr(plo, WRITE_ALARM, INVALID_ALARM);
    if(!failure) pdevSupportGpib->queueWriteRequest(pgpibDpvt,loGpibFinish);
    return(0);
}

static void loGpibFinish(gpibDpvt * pgpibDpvt,int failure)
{
    requestProcessCallback(pgpibDpvt);
}

static void mbbiGpibFinish(gpibDpvt *pgpibDpvt,int failure);
long epicsShareAPI devGpib_initMbbi(mbbiRecord * pmbbi)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    devGpibNames *pdevGpibNames;
    int name_ct;        /* for filling in the name strings */
    char *name_ptr;     /* index into name list array */
    unsigned long *val_ptr;     /* indev into the value list array */

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pmbbi, &pmbbi->inp);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pmbbi);
    pdevGpibNames = devGpibNamesGet(pgpibDpvt);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT|GPIBEFASTI|GPIBEFASTIW))) {
        printf("%s invalid command type for MBBI record in param %d\n",
            pmbbi->name, pgpibDpvt->parm);
        pmbbi->pact = TRUE;
        return (S_db_badField);
    }
    if(pdevGpibNames) {
        if (pdevGpibNames->value == NULL) {
            printf("%s: init_rec_mbbi: MBBI value list wrong for param"
                " #%d\n", pmbbi->name, pgpibDpvt->parm);
            pmbbi->pact = TRUE;
            return (S_db_badField);
        }
        pmbbi->nobt = pdevGpibNames->nobt;
        name_ct = 0;    /* current name string element */
        name_ptr = pmbbi->zrst; /* first name string element */
        val_ptr = &(pmbbi->zrvl);       /* first value element */
        while (name_ct < pdevGpibNames->count) {
            if (name_ptr[0] == 0) {
                strcpy(name_ptr, pdevGpibNames->item[name_ct]);
                *val_ptr = pdevGpibNames->value[name_ct];
            }
            name_ct++;
            name_ptr += sizeof(pmbbi->zrst);
            val_ptr++;
        }
    }
    return (0);
}

long epicsShareAPI devGpib_readMbbi(mbbiRecord * pmbbi)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pmbbi);
    int cmdType;
 
    if(pmbbi->pact) return(0);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    pdevSupportGpib->queueReadRequest(pgpibDpvt,mbbiGpibFinish);
    return(0);
}

static void mbbiGpibFinish(gpibDpvt * pgpibDpvt,int failure)
{
    mbbiRecord *pmbbi = ((mbbiRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    devGpibParmBlock *pdevGpibParmBlock = pgpibDpvt->pdevGpibParmBlock;
    unsigned long value;

    if(failure) {; /*do nothing*/
    } else if (pgpibCmd->convert) {
        failure = pgpibCmd->convert(pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
    } else if (!pgpibDpvt->msg || ! pgpibCmd->format) {
        printf("%s Either no msg buffer or no format\n",pmbbi->name);
        failure = -1;
    } else {/* interpret msg with predefined format and write into rval */
        if(pgpibCmd->type&(GPIBEFASTI|GPIBEFASTIW)) {
            if(pgpibDpvt->efastVal>=0) {
                pmbbi->rval = pgpibDpvt->efastVal;
            } else {
                failure = -1;
            }
        } else {
            if (sscanf(pgpibDpvt->msg, pgpibCmd->format, &value) == 1) {
                pmbbi->rval = value;
            } else {
                /*sscanf did not find or assign the parameter*/
                failure = -1;
                if (*pdevGpibParmBlock->debugFlag) {
                        printf("%s can't convert msg >%s<\n",
                            pmbbi->name, pgpibDpvt->msg);
                }
            }
        }
    }
    if(failure==-1) recGblSetSevr(pmbbi, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void mbbiDirectGpibFinish(gpibDpvt *pgpibDpvt,int failure);
long epicsShareAPI devGpib_initMbbiDirect(mbbiDirectRecord * pmbbiDirect)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    /* do common initialization */
    result = pdevSupportGpib->initRecord(
        (dbCommon *) pmbbiDirect, &pmbbiDirect->inp);
    if(result) return (result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pmbbiDirect);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT))) {
        printf("%s invalid command type for MBBI_DIRECT record in param %d\n",
            pmbbiDirect->name, pgpibDpvt->parm);
        pmbbiDirect->pact = TRUE;
        return (S_db_badField);
    }
    return (0);
}

long epicsShareAPI devGpib_readMbbiDirect(mbbiDirectRecord * pmbbiDirect)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pmbbiDirect);
    int cmdType;
 
    if(pmbbiDirect->pact) return(0);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    pdevSupportGpib->queueReadRequest(pgpibDpvt,mbbiDirectGpibFinish);
    return(0);
}

static void mbbiDirectGpibFinish(gpibDpvt * pgpibDpvt,int failure)
{
    unsigned long value;
    mbbiDirectRecord *pmbbiDirect = ((mbbiDirectRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    devGpibParmBlock *pdevGpibParmBlock = pgpibDpvt->pdevGpibParmBlock;

    if(failure) {; /*do nothing*/
    } else if (pgpibCmd->convert) {
        failure = pgpibCmd->convert(pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
    } else if (!pgpibDpvt->msg || ! pgpibCmd->format) {
        printf("%s Either no msg buffer or no format\n",pmbbiDirect->name);
        failure = -1;
    } else {
        if (sscanf(pgpibDpvt->msg, pgpibCmd->format, &value) == 1) {
            pmbbiDirect->rval = value;
        } else {
            failure = -1;
            if (*pdevGpibParmBlock->debugFlag) {
                printf("%s can't convert msg >%s<\n",
                    pmbbiDirect->name, pgpibDpvt->msg);
            }
        }
    }
    if(failure==-1) recGblSetSevr(pmbbiDirect, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void mbboGpibFinish(gpibDpvt *pgpibDpvt,int failure);
long epicsShareAPI devGpib_initMbbo(mbboRecord * pmbbo)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    devGpibNames *pdevGpibNames;
    int name_ct;        /* for filling in the name strings */
    char *name_ptr;     /* index into name list array */
    unsigned long *val_ptr;     /* indev into the value list array */

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pmbbo, &pmbbo->out);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pmbbo);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBEFASTO|GPIBSOFT))) {
        printf("%s invalid command type for MBBO record in param %d\n",
            pmbbo->name, pgpibDpvt->parm);
        pmbbo->pact = TRUE;
        return (S_db_badField);
    }
    pdevGpibNames = devGpibNamesGet(pgpibDpvt);
    if (pdevGpibNames) {
        if (pdevGpibNames->value == NULL) {
            printf("%s: init_rec_mbbo: MBBO value list wrong for param"
               " #%d\n", pmbbo->name, pgpibDpvt->parm);
            pmbbo->pact = TRUE;
            return (S_db_badField);
        }
        pmbbo->nobt = pdevGpibNames->nobt;
        name_ct = 0;    /* current name string element */
        name_ptr = pmbbo->zrst; /* first name string element */
        val_ptr = &(pmbbo->zrvl);       /* first value element */
        while (name_ct < pdevGpibNames->count) {
            if (name_ptr[0] == 0) {
                strcpy(name_ptr, pdevGpibNames->item[name_ct]);
                *val_ptr = pdevGpibNames->value[name_ct];
            }
            name_ct++;
            name_ptr += sizeof(pmbbo->zrst);
            val_ptr++;
        }
    }
    return (2);
}

long epicsShareAPI devGpib_writeMbbo(mbboRecord * pmbbo)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pmbbo);
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int failure = 0;
 
    if(pmbbo->pact) return(0);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    if(!pgpibCmd->convert) {
        if (pgpibCmd->type&GPIBWRITE) {
            failure = pdevSupportGpib->writeMsgULong(pgpibDpvt,pmbbo->rval);
        } else if (pgpibCmd->type&GPIBEFASTO) {
            pgpibDpvt->efastVal = pmbbo->val;
        }
    }
    if(failure==-1) recGblSetSevr(pmbbo, WRITE_ALARM, INVALID_ALARM);
    if(!failure) pdevSupportGpib->queueWriteRequest(pgpibDpvt,mbboGpibFinish);
    return(0);
}

static void mbboGpibFinish(gpibDpvt * pgpibDpvt,int failure)
{
    requestProcessCallback(pgpibDpvt);
}

static void mbboDirectGpibFinish(gpibDpvt *pgpibDpvt,int failure);
long epicsShareAPI devGpib_initMbboDirect(mbboDirectRecord * pmbboDirect)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *)pmbboDirect,&pmbboDirect->out);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pmbboDirect);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBSOFT))) {
        printf("%s invalid command type for MBBO_DIRECT record in param %d\n",
            pmbboDirect->name, pgpibDpvt->parm);
        pmbboDirect->pact = TRUE;
        return (S_db_badField);
    }
    return (2);
}

long epicsShareAPI devGpib_writeMbboDirect(mbboDirectRecord * pmbboDirect)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pmbboDirect);
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int failure = 0;
 
    if(pmbboDirect->pact) return(0);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    if(!pgpibCmd->convert) {
        if (pgpibCmd->type&GPIBWRITE) {
            failure = pdevSupportGpib->writeMsgULong(pgpibDpvt,pmbboDirect->rval);
        }
    }
    if(failure==-1) recGblSetSevr(pmbboDirect, WRITE_ALARM, INVALID_ALARM);
    if(!failure) pdevSupportGpib->queueWriteRequest(pgpibDpvt,mbboDirectGpibFinish);
    return(0);
}

static void mbboDirectGpibFinish(gpibDpvt * pgpibDpvt,int failure)
{
    requestProcessCallback(pgpibDpvt);
}

static void siGpibFinish(gpibDpvt *pgpibDpvt,int failure);
long epicsShareAPI devGpib_initSi(stringinRecord * psi)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) psi, &psi->inp);
    if(result) return (result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(psi);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT))) {
        printf("%s invalid command type for SI record in param %d\n",
            psi->name, pgpibDpvt->parm);
        psi->pact = TRUE;
        return (S_db_badField);
    }
    return (0);
}

long epicsShareAPI devGpib_readSi(stringinRecord * psi)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(psi);
    int cmdType;
 
    if(psi->pact) return(0);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    pdevSupportGpib->queueReadRequest(pgpibDpvt,siGpibFinish);
    return(0);
}

static void siGpibFinish(gpibDpvt * pgpibDpvt,int failure)
{
    stringinRecord *psi = ((stringinRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);

    if(failure) {; /*do nothing*/
    } else if (pgpibCmd->convert) {
        failure = pgpibCmd->convert(pgpibDpvt,
            pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
    } else if (!pgpibDpvt->msg) {
        printf("%s no msg buffer\n",psi->name);
        failure = -1;
    } else {/* interpret msg with predefined format and write into val */
        char *format = (pgpibCmd->format) ? pgpibCmd->format : "%s";
        int lenVal = sizeof(psi->val);
        int nchars;

        nchars = epicsSnprintf(psi->val,lenVal,format,pgpibDpvt->msg);
        if(nchars>=lenVal) {
            psi->val[lenVal-1] = 0;
            printf("%s %d characters were truncated\n",
                psi->name,(nchars-lenVal+1));
            failure = -1;
        }
        psi->udf = FALSE;
    }
    if(failure==-1) recGblSetSevr(psi, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void soGpibFinish(gpibDpvt *pgpibDpvt,int failure);
long epicsShareAPI devGpib_initSo(stringoutRecord * pso)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pso, &pso->out);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pso);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBSOFT))) {
        printf("%s invalid command type for SO record in param %d\n",
            pso->name, pgpibDpvt->parm);
        pso->pact = TRUE;
        return (S_db_badField);
    }
    return (0);
}

long epicsShareAPI devGpib_writeSo(stringoutRecord * pso)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pso);
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int failure = 0;
 
    if(pso->pact) return(0);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    if(!pgpibCmd->convert) {
        if (pgpibCmd->type&GPIBWRITE) {/* only if needs formatting */
            failure = pdevSupportGpib->writeMsgString(pgpibDpvt, pso->val);
        }
    }
    if(failure==-1) recGblSetSevr(pso, WRITE_ALARM, INVALID_ALARM);
    if(!failure) pdevSupportGpib->queueWriteRequest(pgpibDpvt,soGpibFinish);
    return(0);
}

static void soGpibFinish(gpibDpvt * pgpibDpvt,int failure)
{
    requestProcessCallback(pgpibDpvt);
}

static void wfGpibFinish(gpibDpvt *pgpibDpvt,int failure);
long epicsShareAPI devGpib_initWf(waveformRecord * pwf)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    gpibCmd *pgpibCmd;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pwf, &pwf->inp);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pwf);
    pgpibCmd = gpibCmdGet(pgpibDpvt);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD)) {
        if((!pgpibCmd->convert) && (pwf->ftvl!=menuFtypeCHAR)) {
            printf("%s ftvl != CHAR but no convert\n", pwf->name);
            pwf->pact = 1;
            return (S_db_badField);
        }
    } else if(!(cmdType&(GPIBSOFT|GPIBWRITE|GPIBCMD|GPIBACMD))) {
        printf("%s invalid command type for WF record in param %d\n",
            pwf->name, pgpibDpvt->parm);
        pwf->pact = TRUE;
        return (S_db_badField);
    }
    return (0);
}

long epicsShareAPI devGpib_readWf(waveformRecord * pwf)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pwf);
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int failure = 0;
 
    if(pwf->pact) return(0);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    if(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD)) {
        pdevSupportGpib->queueReadRequest(pgpibDpvt,wfGpibFinish);
    } else { /*Must be Output Operation*/
        if(!pgpibCmd->convert && (pgpibCmd->type&GPIBWRITE)) {
            if(pwf->ftvl!=menuFtypeCHAR) {
                printf("%s ftvl != CHAR but no convert\n", pwf->name);
                pwf->pact = 1;
                failure = -1;
            } else {
                failure = pdevSupportGpib->writeMsgString(pgpibDpvt, pwf->bptr);
            }
        }
        if(failure==-1) recGblSetSevr(pwf, WRITE_ALARM, INVALID_ALARM);
        if(!failure) pdevSupportGpib->queueWriteRequest(pgpibDpvt,wfGpibFinish);
    }
    return(0);
}

static void wfGpibFinish(gpibDpvt * pgpibDpvt,int failure)
{
    waveformRecord *pwf = (waveformRecord *)pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);

    if(failure) {; /*do nothing*/
    } else if(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD)) {
        if(pgpibCmd->convert) {
            failure = pgpibCmd->convert(pgpibDpvt,
                pgpibCmd->P1,pgpibCmd->P2,pgpibCmd->P3);
        } else if(pwf->ftvl!=menuFtypeCHAR) {
            printf("%s ftvl != CHAR but no convert\n",pwf->name);
            failure = -1; 
        } else {
            char *format = (pgpibCmd->format) ? pgpibCmd->format : "%s";
            int lenDest = pwf->nelm;
            int lenmsg = pgpibDpvt->msgInputLen;
            char *pdest = (char *)pwf->bptr;
            int nchars;

            nchars = epicsSnprintf(pdest,lenDest,format,pgpibDpvt->msg);
            if(nchars>=lenDest) {
                 pdest[lenDest-1] = 0;
                 printf("%s %d characters were truncated\n",
                     pwf->name,(nchars-lenDest+1));
                 failure = -1;
                 lenmsg = lenDest;
            }
            pwf->udf = FALSE;
            pwf->nord = lenmsg;
        }
    }
    if(failure==-1) recGblSetSevr(pwf, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}
