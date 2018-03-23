/****************************************************************************************
 * Copyright (c) 2008~2009 All Rights Resverved by
 *  G-Net Integrated Service co. Ltd. 
 ****************************************************************************************/
/**
 * @file test.cpp
 * @brief --YOUR BRIFE HERE-- 
 *
 * @author root
 *
 * @date Mar. 23 2018
 *
 * @version 0.1.1
 *
 * @warning  w1vwvw\n
 *          
* @par 需求:
 *        REQ1.10[Tang]: svn://......
 * Revision History 
 * @if  CR/PR ID Author	   Date		  Major Change @endif
 * @bug CR0001   root Mar. 23 2018 create version 0.0.1\n
 ****************************************************************************************/
/*------------------------------- HEADER FILE INCLUDES ---------------------------------*/
#include <stdlib.h>
#include <stdio.h>
#include <config.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/TestCase.h>
/*------------------------------------  DEFINITION ------------------------------------*/
/*----------------------------------- ENUMERATIONS ------------------------------------*/
/*-------------------------- STRUCT/UNION/CLASS DATA TYPES ----------------------------*/
/*-------------------------------------- CONSTANTS ------------------------------------*/
/*------------------------------------- GLOBAL DATA -----------------------------------*/
/*----------------------------- LOCAL FUNCTION PROTOTYPES -----------------------------*/
/*-------------------------------- FUNCTION DEFINITION --------------------------------*/
/**
 * @brief 主函数，仅仅用于示例
 * @details 系统启动后，经过初始化，第一个用户函数调用，
 *          就调用该函数，当该函数返回时系统退出该程序。
 *
 * @param[in] argc {number of the program arguments}
 * @param[in] argv {argument array, char **}
 *
 * @exception {} {}
 * @return {0 for succuess, otherwise failed}
 *
 * @par 代码示例:
 * @code
 *  {}
 * @endcode
 * @par 需求:
 *		REQ1.10[Tang]: svn://......
 ***********************************************************************************/
int main( int argc, char ** argv) {
  CppUnit::TextUi::TestRunner runner;
  /*
    add the test case as ...
    runner.addTest( new ...TestCase );
  */
  return !runner.run();
}

