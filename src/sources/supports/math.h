////////////////////////////////////////////////////////////////////////////////
// CONFIDENTIAL and PROPRIETARY software of Magewell Electronics Co., Ltd.
// Copyright (c) 2011-2014 Magewell Electronics Co., Ltd. (Nanjing)
// All rights reserved.
// This copyright notice MUST be reproduced on all authorized copies.
////////////////////////////////////////////////////////////////////////////////
#ifndef __MATH_H__
#define __MATH_H__

#define _max(a,b) 							(((a) > (b)) ? (a) : (b))
#define _min(a,b) 							(((a) < (b)) ? (a) : (b))
#define _limit(val, min_val, max_val)		_min(max_val, _max(min_val, val))

int i_cos(short angle);

int i_sin(short angle);

int GCD(int a, int b);

#endif /* __MATH_H__ */
