/*
 * Copyright (c) 2004, Laminar Research.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

 // TODO
 // - Clean up writing code a bit to handle decimals better.

#include "XObjReadWrite.h"
#include "XObjDefs.h"
#include "AssertUtils.h"
#include <math.h>

#ifndef CRLF
	#if APL
		#define CRLF "\n"
	#elif LIN
		#define CRLF "\n"
	#elif IBM
		#define CRLF "\n"
	#else
		#error no platform defined
	#endif
#endif

static const char * dataref_or_none(const char * d)
{
	if(d[0] == 0)
		return "none";
	return d;
}

/****************************************************************************************
 * FILE SCANNING UTILS
 ****************************************************************************************/

typedef unsigned char 	xbyt;
typedef int				xint;
typedef float			xflt;
#define xtrue			true
#define xfals			false

// Identify chars that indicate a new line
inline xint  TXT_MAP_eoln(const xbyt* c)
{
	return(*c==0 || *c==13 || *c=='\n');
}

// Identify chars that separate with whitespace
inline xint  TXT_MAP_space(const xbyt* c)
{
	return(*c=='\t' || *c==' ');
}

// Consume all white space and newlines to get to the beginning of the next non-blank line.
// This implies that leading and trailing white space for a newline is ignored, as are blank lines.
inline void  TXT_MAP_finish_line(xbyt*& c, const xbyt* c_max)
{
	while(c< c_max && (TXT_MAP_eoln(c) || TXT_MAP_space(c))) ++c;	// Scan through whitespace too - helps find blank lines with chars on them!
}

// Build a string from the first char to the end of the line (including trailing whitespace).
inline void TXT_MAP_str_scan_eoln(xbyt*& c,const xbyt* c_max,string* input)
{
	while(c< c_max && TXT_MAP_space(c)) ++c;

	xbyt* c1=c;
	while(c<c_max && !TXT_MAP_eoln(c)) ++c;
	xbyt* c2=c;

	if(input)*input=string(c1,c2);

	TXT_MAP_finish_line(c,c_max);	// call the proc to go to EOLN here since windows can have TWO eoln chars!
}

// Build a string of the first non-whitespace word.
inline void TXT_MAP_str_scan_space(xbyt*& c,const xbyt* c_max,string* input)
{
	while(c<c_max && TXT_MAP_space(c)) ++c;

	xbyt* c1=c;
	while(c<c_max && !TXT_MAP_space(c) && !TXT_MAP_eoln(c)) ++c;
	xbyt* c2=c;

	if(input)*input=string(c1,c2);
}

inline xint TXT_MAP_has_word(xbyt*& c,const xbyt* c_max)
{
	while(c<c_max && TXT_MAP_space(c)) ++c;
	if(c==c_max) return xfals;
	return !TXT_MAP_eoln(c);
}

inline xint TXT_MAP_str_match_space(xbyt*& c,const xbyt* c_max,const char* input, bool eol_ok)
{
	while(c<c_max && TXT_MAP_space(c)) ++c;

	xbyt* c1=c;
	while(c<c_max && *c==*input && *input != 0) { ++c; ++input; }

	if (*input==0 && 		   TXT_MAP_space(c)) return xtrue;
	if (*input==0 && eol_ok && TXT_MAP_eoln (c)) return xtrue;
	c=c1;
	return xfals;
}

inline xflt TXT_MAP_flt_scan(xbyt*& c,const xbyt* c_max, bool go_next_line)
{
	while(c< c_max && (
		TXT_MAP_space(c) ||
		(go_next_line && TXT_MAP_eoln(c))))	++c;

	xflt ret_val	=0;
	xint decimals	=0;
	xint negative	=xfals;
	xint has_decimal=xfals;

	while(c<c_max && !TXT_MAP_space(c) && !TXT_MAP_eoln(c))
	{
			 if(*c=='-')negative	=xtrue;
		else if(*c=='+')negative	=xfals;
		else if(*c=='.')has_decimal	=xtrue;
		else
		{
			ret_val=(10*ret_val)+*c-'0';
			if(has_decimal)decimals++;
		}
		++c;
	}
	return ret_val/pow((xflt)10,(xflt)decimals)*((negative)?-1.0:1.0);
}

inline xint TXT_MAP_int_scan(xbyt*& c,const xbyt* c_max, bool go_next_line)
{
	while(c< c_max && (
		(				 TXT_MAP_space(c)) ||
		(go_next_line && TXT_MAP_eoln(c))))		++c;

	xint retval	=0;
	xint sign_mult=1;

	if(c<c_max && *c=='-'){sign_mult=-1;	++c;}
	if(c<c_max && *c=='+'){sign_mult= 1;	++c;}

	while(c<c_max && !TXT_MAP_space(c) && !TXT_MAP_eoln(c))
	{
		retval=(10*retval)+*c-'0';
		++c;
	}
	return sign_mult * retval;
}

inline xint TXT_MAP_continue(const xbyt* c,const xbyt* c_max)
{
	return (c < c_max);
}

/****************************************************************************************
 * OBJ 2/7 REAd
 ****************************************************************************************/
bool	XObjRead(const char * inFile, XObj& outObj)
{
		int 	count;
		vec_rgb	vrgb;
		vec_tex	vst, vst2;

	outObj.texture.clear();
	outObj.cmds.clear();

	float scanned_st_rgb[4][3]={0,0,0 , 0,0,0,// [corner][color or st]
                               0,0,0 , 0,0,0};

	outObj.cmds.clear();

	/*********************************************************************
	 * READ FILE INTO MEM
	 *********************************************************************/

	FILE * objFile = fopen(inFile, "rb");
	if (!objFile) return false;
	fseek(objFile,0L,SEEK_END);
	int filesize = ftell(objFile);
	fseek(objFile,0L,SEEK_SET);
	unsigned char * mem_buf = (unsigned char *) malloc(filesize);
	if (mem_buf == NULL) { fclose(objFile); return false; }
	if (fread(mem_buf, 1, filesize, objFile) != filesize)
	{
		free(mem_buf); fclose(objFile); return false;
	}
	fclose(objFile);

	/*********************************************************************
	 * READ HEADER
	 *********************************************************************/

	unsigned char *	cur_ptr = mem_buf;
	unsigned char *	end_ptr = mem_buf + filesize;

	// LINE 1: A/I - who cares?!?
	TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);

	// LINE 2: version
	int vers = TXT_MAP_int_scan(cur_ptr, end_ptr, xfals);
	TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);

	// LINE 3: "OBJ"
	if (vers == 700)
	{
		if (!TXT_MAP_str_match_space(cur_ptr, end_ptr, "OBJ", xtrue))
			vers = 0;
		TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);
	}

	// If we don't have a good version, bail.
	if (vers != 2 && vers != 700)
	{
		free(mem_buf);
		return false;
	}

	// LINE 4: Texture
	TXT_MAP_str_scan_space(cur_ptr, end_ptr, &outObj.texture);
	TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);


	/************************************************************
	 * READ GEOMETRIC COMMANDS
	 ************************************************************/

	bool	stop = false;

	while (!stop && TXT_MAP_continue(cur_ptr, end_ptr))
	{
		XObjCmd	cmd;
		double xav, yav, zav;

		/************************************************************
		 * OBJ2 SCANNING
		 ************************************************************/
		if (vers == 2)
		{
			int obj2_op = TXT_MAP_int_scan(cur_ptr, end_ptr, xfals);
			switch(obj2_op) {
			case 1:
			case 2:
				// Points and lines.  The header line contains the color x10.
				// The type (pt or line) tell how much geometry follows.
				cmd.cmdID = (obj2_op == 1) ? obj_Light : obj_Line;
				cmd.cmdType = type_PtLine;
				count = obj2_op;
				scanned_st_rgb[0][0]=scanned_st_rgb[1][0]=TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals); // r
				scanned_st_rgb[0][1]=scanned_st_rgb[1][1]=TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals); // g
				scanned_st_rgb[0][2]=scanned_st_rgb[1][2]=TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals); // b
				TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);

				// Sets of x,y,z follows.
				for (int t = 0; t < count; ++t)
				{
					vrgb.v[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vrgb.v[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vrgb.v[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vrgb.rgb[0] = scanned_st_rgb[t][0];
					vrgb.rgb[1] = scanned_st_rgb[t][1];
					vrgb.rgb[2] = scanned_st_rgb[t][2];
					cmd.rgb.push_back(vrgb);
				}
				outObj.cmds.push_back(cmd);
				TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);
				break;

			case 3:
			case 4:
			case 5:
			case 6:
			case 7:
				// Finite-size polygons.  The header line contains s1, s2, t1, t2.
				cmd.cmdID = (obj2_op == 5) ? obj_Quad_Hard : obj_Quad;
				if (obj2_op == 3) cmd.cmdID = obj_Tri;
				cmd.cmdType = type_Poly;
				count = obj2_op;
				if (count > 4) count = 4;
				// Make sure to 'spread' the 4 S/T coords to 8 points.  This is
				// because
				scanned_st_rgb[2][0]=scanned_st_rgb[3][0]=TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);	// s1
				scanned_st_rgb[0][0]=scanned_st_rgb[1][0]=TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);	// s2
				scanned_st_rgb[1][1]=scanned_st_rgb[2][1]=TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);	// t1
				scanned_st_rgb[0][1]=scanned_st_rgb[3][1]=TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);  	// t2

				xav = yav = zav = 0.0;

				TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);
				// Read sets of 3 points.
				for (int t = 0; t < count; ++t)
				{
					vst.v[0] =  TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vst.v[1] =  TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vst.v[2] =  TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					xav += vst.v[0];
					yav += vst.v[1];
					zav += vst.v[2];
					vst.st[0] = scanned_st_rgb[t][0];
					vst.st[1] = scanned_st_rgb[t][1];
					cmd.st.push_back(vst);
				}
				outObj.cmds.push_back(cmd);
				if (obj2_op == 6 || obj2_op == 7)
				{
					xav *= 0.25;
					yav *= 0.25;
					zav *= 0.25;
					XObjCmd	smoke_cmd;
					smoke_cmd.cmdID = (obj2_op == 6 ? obj_Smoke_Black : obj_Smoke_White);
					smoke_cmd.cmdType = type_Attr;
					smoke_cmd.attributes.push_back(xav);
					smoke_cmd.attributes.push_back(yav);
					smoke_cmd.attributes.push_back(zav);
					smoke_cmd.attributes.push_back(1.0);
					outObj.cmds.push_back(smoke_cmd);
				}
				TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);
				break;

			case 99:
				TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);
				stop = true;
				// 99 is the end token for obj2 files.
				break;
			default:
				// Negative numbers equal positive
				// quad strips.  The count is the number
				// of vertex pairs, since they are always even.
				if (obj2_op >= 0)
					return false;
				count = -obj2_op;
				cmd.cmdID = obj_Quad_Strip;
				cmd.cmdType = type_Poly;
				TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);
				// Read a pair of x,y,z,s,t coords.
				while (count--)
				{
					vst.v[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vst.v[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vst.v[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vst2.v[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vst2.v[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vst2.v[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vst.st[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vst2.st[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vst.st[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vst2.st[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					cmd.st.push_back(vst);
					cmd.st.push_back(vst2);
				}
				outObj.cmds.push_back(cmd);
				TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);
				break;
			}

		} else {

			/************************************************************
			 * OBJ7 SCANNING
			 ************************************************************/

			string	cmd_name;
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &cmd_name);
			int cmd_id = FindObjCmd(cmd_name.c_str(), false);

			cmd.cmdType = gCmds[cmd_id].cmd_type;
			cmd.cmdID = gCmds[cmd_id].cmd_id;
			count = gCmds[cmd_id].elem_count;

			switch(gCmds[cmd_id].cmd_type) {
			case type_None:
				if (cmd.cmdID == obj_End)
					stop = true;
				TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);
				break;
			case type_PtLine:

				TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);
				while (count--)
				{
					vrgb.v[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vrgb.v[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vrgb.v[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vrgb.rgb[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vrgb.rgb[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vrgb.rgb[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);

					cmd.rgb.push_back(vrgb);
				}
				outObj.cmds.push_back(cmd);
				TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);
				break;

			case type_Poly:

				if (count == 0) count = TXT_MAP_int_scan(cur_ptr, end_ptr, xfals);
				TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);

				while (count--)
				{
					vst.v[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vst.v[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vst.v[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vst.st[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);
					vst.st[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xtrue);

					cmd.st.push_back(vst);
				}
				outObj.cmds.push_back(cmd);
				TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);
				break;
			case type_Attr:

				for (int n = 0; n < count; ++n)
					cmd.attributes.push_back(TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals));

				outObj.cmds.push_back(cmd);
				TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);
				break;
			}

		}	// Obj 7 Paraser

	} // While loop

	free(mem_buf);
	return true;
}

/****************************************************************************************
 * OBJ 7 WRITE
 ****************************************************************************************/

bool	XObjWrite(const char * inFile, const XObj& inObj)
{
	FILE * fi = fopen(inFile, "wb");
	if (!fi) return false;

	fprintf(fi,"%c" CRLF, APL ? 'A' : 'I');
	fprintf(fi,"700" CRLF);
	fprintf(fi,"OBJ" CRLF CRLF);
	fprintf(fi,"%s\t\t//" CRLF CRLF, inObj.texture.c_str());

	for (vector<XObjCmd>::const_iterator iter = inObj.cmds.begin(); iter != inObj.cmds.end(); ++iter)
	{
		int 	index	= FindIndexForCmd(iter->cmdID);
		switch(iter->cmdType) {
		case type_PtLine:

			if (gCmds[index].elem_count == 0)
				fprintf(fi,"%s %llu\t\t//" CRLF, gCmds[index].name, (unsigned long long)iter->rgb.size());
			else
				fprintf(fi,"%s\t\t//" CRLF, gCmds[index].name);

			for (vector<vec_rgb>::const_iterator riter = iter->rgb.begin();
				riter != iter->rgb.end(); ++riter)
			{
				fprintf(fi,"%f %f %f    %f %f %f" CRLF,
					riter->v[0], riter->v[1], riter->v[2],
					riter->rgb[0], riter->rgb[1], riter->rgb[2]);
			}
			fprintf(fi,CRLF);
			break;


		case type_Poly:

			if (gCmds[index].elem_count == 0)
				fprintf(fi,"%s %llu\t\t//" CRLF, gCmds[index].name, (unsigned long long)iter->st.size());
			else
				fprintf(fi,"%s\t\t//" CRLF, gCmds[index].name);

			for (vector<vec_tex>::const_iterator siter = iter->st.begin();
				siter != iter->st.end(); ++siter)
			{
				fprintf(fi,"%f %f %f    %f %f" CRLF,
					siter->v[0], siter->v[1], siter->v[2],
					siter->st[0], siter->st[1]);
			}
			fprintf(fi,CRLF);
			break;


		case type_Attr:
			fprintf(fi,"%s",gCmds[index].name);
			for (vector<float>::const_iterator aiter = iter->attributes.begin();
				aiter != iter->attributes.end(); ++aiter)
			{
				fprintf(fi," %f", *aiter);
			}
			fprintf(fi, "\t\t//" CRLF CRLF);
			break;
		}
	}

	fprintf(fi,"end\t\t//" CRLF);

	fclose(fi);
	return true;
}

/****************************************************************************************
 * OBJ 8 READ
 ****************************************************************************************/
bool	XObj8Read(const char * inFile, XObj8& outObj)
{
		int 	n;

	outObj.texture.clear();
	outObj.texture_lit.clear();
	outObj.texture_normal_map.clear();
//	outObj.texture_nrm.clear();
	outObj.indices.clear();
	outObj.geo_tri.clear(8);
	outObj.geo_lines.clear(6);
	outObj.geo_lights.clear(6);
	outObj.animation.clear();
	outObj.lods.clear();
	outObj.use_metalness = 0;
	outObj.glass_blending = 0;
	outObj.fixed_heading = -1.0;

	/*********************************************************************
	 * READ FILE INTO MEM
	 *********************************************************************/

	FILE * objFile = fopen(inFile, "rb");
	if (!objFile) return false;
	fseek(objFile,0L,SEEK_END);
	int filesize = ftell(objFile);
	fseek(objFile,0L,SEEK_SET);
	unsigned char * mem_buf = (unsigned char *) malloc(filesize);
	if (mem_buf == NULL) { fclose(objFile); return false; }
	if (fread(mem_buf, 1, filesize, objFile) != filesize)
	{
		free(mem_buf); fclose(objFile); return false;
	}
	fclose(objFile);

	/*********************************************************************
	 * READ HEADER
	 *********************************************************************/

	unsigned char *	cur_ptr = mem_buf;
	unsigned char *	end_ptr = mem_buf + filesize;

	// LINE 1: A/I - who cares?!?
	TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);

	// LINE 2: version
	int vers = TXT_MAP_int_scan(cur_ptr, end_ptr, xfals);
	TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);

	// LINE 3: "OBJ"
	if (!TXT_MAP_str_match_space(cur_ptr, end_ptr, "OBJ", xtrue))
		vers = 0;
	TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);

	// If we don't have a good version, bail.
	if (vers != 800)
	{
		free(mem_buf);
		return false;
	}

	/************************************************************
	 * READ GEOMETRIC COMMANDS
	 ************************************************************/

	bool	stop = false;

	int trimax, linemax, lightmax, idxmax;
	int tricount = 0, linecount = 0, lightcount = 0, idxcount = 0;
	float	stdat[8];

	XObjCmd8	cmd;
	XObjAnim8	animation;

	outObj.lods.push_back(XObjLOD8());
	outObj.lods.back().lod_near = outObj.lods.back().lod_far = 0;

	while (!stop && TXT_MAP_continue(cur_ptr, end_ptr))
	{
		bool ate_eoln = false;
		// TEXTURE <tex>
		if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "TEXTURE", xfals))
		{
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &outObj.texture);
		}
		// TEXTURE_LIT <tex>
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "TEXTURE_LIT", xfals))
		{
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &outObj.texture_lit);
		}
		// TEXTURE_NORMAL <tex>
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "TEXTURE_NORMAL", xfals))
		{
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &outObj.texture_normal_map);
		}
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "TEXTURE_DRAPED", xfals))
		{
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &outObj.texture_draped);
		}
		// POINT_COUNTS tris lines lites geo indices
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "POINT_COUNTS", xfals))
		{
			trimax = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			linemax = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			lightmax = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			idxmax = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);

			outObj.indices.resize(idxmax);
			outObj.geo_tri.clear(8);
			outObj.geo_tri.resize(trimax);
			outObj.geo_lines.clear(6);
			outObj.geo_lines.resize(linemax);
			outObj.geo_lights.clear(6);
			outObj.geo_lights.resize(lightmax);
		}
		// VT <x> <y> <z> <nx> <ny> <nz> <s> <t>
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "VT", xfals))
		{
			if (tricount >= trimax) break;
			stdat[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[3] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[4] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[5] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[6] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[7] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			outObj.geo_tri.set(tricount++, stdat);
		}
		// VLINE <x> <y> <z> <r> <g> <b>
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "VLINE", xfals))
		{
			if (linecount >= linemax) break;
			stdat[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[3] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[4] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[5] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			outObj.geo_lines.set(linecount++, stdat);
		}
		// VLIGHT <x> <y> <z> <r> <g> <b>
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "VLIGHT", xfals))
		{
			if (lightcount >= lightmax) break;
			stdat[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[3] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[4] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			stdat[5] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			outObj.geo_lights.set(lightcount++, stdat);
		}
		// IDX <n>
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "IDX", xfals))
		{
			if (idxcount >= idxmax) break;
			outObj.indices[idxcount++] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
		}
		// IDX10 <n> x 10
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "IDX10", xfals))
		{
			if (idxcount >= idxmax) break;
			for (n = 0; n < 10; ++n)
				outObj.indices[idxcount++] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
		}
		// TRIS offset count
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "TRIS", xfals))
		{
			cmd.cmd = obj8_Tris;
			cmd.idx_offset = TXT_MAP_int_scan(cur_ptr, end_ptr, xfals);
			cmd.idx_count = TXT_MAP_int_scan(cur_ptr, end_ptr, xfals);
			outObj.lods.back().cmds.push_back(cmd);
		}
		// LINES offset count
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "LINES", xfals))
		{
			cmd.cmd = obj8_Lines;
			cmd.idx_offset = TXT_MAP_int_scan(cur_ptr, end_ptr, xfals);
			cmd.idx_count = TXT_MAP_int_scan(cur_ptr, end_ptr, xfals);
			outObj.lods.back().cmds.push_back(cmd);
		}
		// LIGHTS offset count
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "LIGHTS", xfals))
		{
			cmd.cmd = obj8_Lights;
			cmd.idx_offset = TXT_MAP_int_scan(cur_ptr, end_ptr, xfals);
			cmd.idx_count = TXT_MAP_int_scan(cur_ptr, end_ptr, xfals);
			outObj.lods.back().cmds.push_back(cmd);
		}
		// ATTR_LOD near far
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_LOD", xtrue))
		{
			if (outObj.lods.back().lod_far != 0)	outObj.lods.push_back(XObjLOD8());
			outObj.lods.back().lod_near = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			outObj.lods.back().lod_far = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
		}
		// ANIM_rotate x y z r1 r2 v1 v2 dref
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "ANIM_rotate", xfals))
		{
			animation.keyframes.clear();
			animation.cmd = anim_Rotate;
			animation.loop = 0.0f;
			cmd.cmd = anim_Rotate;
			cmd.idx_offset = outObj.animation.size();
			animation.loop = 0.0f;
			outObj.lods.back().cmds.push_back(cmd);
			animation.keyframes.push_back(XObjKey());
			animation.keyframes.push_back(XObjKey());
			animation.axis[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.axis[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.axis[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.keyframes[0].v[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.keyframes[1].v[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.keyframes[0].key = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.keyframes[1].key = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &animation.dataref);
			outObj.animation.push_back(animation);
		}
		// ANIM_trans x1 y1 z1 x2 y2 z2 v1 v2 dref
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "ANIM_trans", xfals))
		{
			animation.keyframes.clear();
			animation.cmd = anim_Translate;
			cmd.cmd = anim_Translate;
			animation.loop = 0.0f;
			cmd.idx_offset = outObj.animation.size();
			animation.loop = 0.0f;
			outObj.lods.back().cmds.push_back(cmd);
			animation.keyframes.push_back(XObjKey());
			animation.keyframes.push_back(XObjKey());
			animation.keyframes[0].v[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.keyframes[0].v[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.keyframes[0].v[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.keyframes[1].v[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.keyframes[1].v[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.keyframes[1].v[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.keyframes[0].key= TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.keyframes[1].key = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &animation.dataref);
			outObj.animation.push_back(animation);
		}
		// ANIM_begin
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "ANIM_begin", xfals))
		{
			cmd.cmd = anim_Begin;
			outObj.lods.back().cmds.push_back(cmd);
		}
		// ANIM_end
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "ANIM_end", xfals))
		{
			cmd.cmd = anim_End;
			outObj.lods.back().cmds.push_back(cmd);
		}
/******************************************************************************************************************************/
		// LIGHT_CUSTOM <x> <y> <z> <r> <g> <b> <a> <s><s1> <t1> <s2> <t2> <dataref>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"LIGHT_CUSTOM", xfals))
		{
			cmd.cmd = obj8_LightCustom;
			for (n = 0; n < 12; ++n)
				cmd.params[n] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&cmd.name);
			outObj.lods.back().cmds.push_back(cmd);
		}
		// LIGHT_NAMED <name> <x> <y> <z>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"LIGHT_NAMED", xfals))
		{
			cmd.cmd = obj8_LightNamed;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&cmd.name);
			for (n = 0; n < 3; ++n)
				cmd.params[n] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			cmd.idx_count = 0;
			outObj.lods.back().cmds.push_back(cmd);			
		}
		// LIGHT_PARAM <name> <x> <y> <z>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"LIGHT_PARAM", xfals))
		{
			cmd.cmd = obj8_LightNamed;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&cmd.name);
			for (n = 0; n < 3; ++n)
				cmd.params[n] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			cmd.idx_count = 0;

			// Ben says: some objs have comment on the param light line - x-plane ignores anything past the
			// known param count since it has the lights.txt file. For now, simply ignore anything past the end.
			while(TXT_MAP_has_word(cur_ptr,end_ptr) && cmd.idx_count < 9)
			{
				cmd.params[cmd.idx_count+3] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);				
				++cmd.idx_count;
			}
			outObj.lods.back().cmds.push_back(cmd);			
		}
		// ATTR_layer_group <group name> <offset>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_layer_group", xfals))
		{
			cmd.cmd = attr_Layer_Group;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&cmd.name);
			cmd.params[0] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			outObj.lods.back().cmds.push_back(cmd);
		}
		// ATTR_hard [<type>]
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_hard", xtrue))
		{
			cmd.cmd = attr_Hard;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&cmd.name);
			if (cmd.name.empty()) cmd.name = "object";
			outObj.lods.back().cmds.push_back(cmd);
		}
		// ATTR_hard_deck [<type>]
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_hard_deck", xtrue))
		{
			cmd.cmd = attr_Hard_Deck;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&cmd.name);
			if (cmd.name.empty()) cmd.name = "object";
			outObj.lods.back().cmds.push_back(cmd);
		}
		// ATTR_no_blend <level>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_no_blend", xtrue))
		{
			cmd.cmd = attr_No_Blend;
			cmd.params[0] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			if (cmd.params[0] == 0.0) cmd.params[0] = 0.5;
			outObj.lods.back().cmds.push_back(cmd);
		}
		// ANIM_hide <v1> <v2> <dataref>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ANIM_hide", xfals))
		{
			animation.keyframes.clear();
			animation.cmd = anim_Hide;
			cmd.cmd = anim_Hide;
			animation.loop = 0.0f;
			cmd.idx_offset = outObj.animation.size();
			animation.loop = 0.0f;
			outObj.lods.back().cmds.push_back(cmd);
			animation.keyframes.push_back(XObjKey());
			animation.keyframes.push_back(XObjKey());
			animation.keyframes[0].key = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.keyframes[1].key = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &animation.dataref);
			outObj.animation.push_back(animation);
		}
		// ANIM_show <v1> <v2> <dataref>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ANIM_show", xfals))
		{
			animation.keyframes.clear();
			animation.cmd = anim_Show;
			cmd.cmd = anim_Show;
			animation.loop = 0.0f;
			cmd.idx_offset = outObj.animation.size();
			animation.loop = 0.0f;
			outObj.lods.back().cmds.push_back(cmd);
			animation.keyframes.push_back(XObjKey());
			animation.keyframes.push_back(XObjKey());
			animation.keyframes[0].key = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.keyframes[1].key = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &animation.dataref);
			outObj.animation.push_back(animation);
		}
/******************************************************************************************************************************/
		// ANIM_rotate_begin x y z dref
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ANIM_rotate_begin", xfals))
		{
			animation.keyframes.clear();
			animation.cmd = anim_Rotate;
			cmd.cmd = anim_Rotate;
			animation.loop = 0.0f;
			cmd.idx_offset = outObj.animation.size();
			animation.loop = 0.0f;
			outObj.lods.back().cmds.push_back(cmd);
			animation.axis[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.axis[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			animation.axis[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &animation.dataref);
			outObj.animation.push_back(animation);
		}
		// ANIM_trans_begin dref
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ANIM_trans_begin", xfals))
		{
			animation.keyframes.clear();
			animation.cmd = anim_Translate;
			animation.loop = 0.0f;
			cmd.cmd = anim_Translate;
			cmd.idx_offset = outObj.animation.size();
			animation.loop = 0.0f;
			outObj.lods.back().cmds.push_back(cmd);
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &animation.dataref);
			outObj.animation.push_back(animation);
		}
		// ANIM_rotate_key v r
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ANIM_rotate_key", xfals))
		{
			outObj.animation.back().keyframes.push_back(XObjKey());
			outObj.animation.back().keyframes.back().key = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			outObj.animation.back().keyframes.back().v[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
		}
		// ANIM_trans_key v x y z
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ANIM_trans_key", xfals))
		{
			outObj.animation.back().keyframes.push_back(XObjKey());
			outObj.animation.back().keyframes.back().key = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			outObj.animation.back().keyframes.back().v[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			outObj.animation.back().keyframes.back().v[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			outObj.animation.back().keyframes.back().v[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
		}
		// ANIM_rotate_end
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ANIM_rotate_end", xtrue))
		{
		}
		// ANIM_trans_end
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ANIM_trans_end", xtrue))
		{
		}
		// ANIM_keyframe_loop <loop>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ANIM_keyframe_loop", xfals))
		{
			outObj.animation.back().loop = TXT_MAP_flt_scan(cur_ptr,end_ptr, xfals);
		}
/******************************************************************************************************************************/
		// COCKPIT_REGION
/******************************************************************************************************************************/
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"COCKPIT_REGION", false))
		{
			outObj.regions.push_back(XObjPanelRegion8());
			outObj.regions.back().left   = TXT_MAP_int_scan(cur_ptr, end_ptr, xfals);
			outObj.regions.back().bottom = TXT_MAP_int_scan(cur_ptr, end_ptr, xfals);
			outObj.regions.back().right  = TXT_MAP_int_scan(cur_ptr, end_ptr, xfals);
			outObj.regions.back().top    = TXT_MAP_int_scan(cur_ptr, end_ptr, xfals);
		}
/******************************************************************************************************************************/
		// MANIPS (920)
/******************************************************************************************************************************/
		// ATTR_manip_none
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_manip_none", true))
		{
			cmd.cmd = attr_Manip_None;
			outObj.lods.back().cmds.push_back(cmd);
		}
		// ATTR_manip_drag_xy <cursor> <dx> <dy> <v1min> <v1max> <v2min> <v2max> <dref1> <dref> <tooltip>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_manip_drag_xy", false))
		{
			cmd.cmd = attr_Manip_Drag_2d;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			manip.axis[0] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.axis[1] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v1_min = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v1_max = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v2_min = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v2_max = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref2);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=true;
			outObj.manips.push_back(manip);
		}
		// ATTR_manip_drag_axis <cursor> <dx> <dy> <dz> <v1> <v2> <dataref> <tooltip>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_manip_drag_axis", false))
		{
			cmd.cmd = attr_Manip_Drag_Axis;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			manip.axis[0] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.axis[1] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.axis[2] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v1_min = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v1_max = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=true;
			outObj.manips.push_back(manip);
		}
		// ATTR_manip_command <currsor> <cmnd> <tooltip>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_manip_command", false))
		{
			cmd.cmd = attr_Manip_Command;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=true;
			outObj.manips.push_back(manip);
		}
		// ATTR_manip_command_axis <cursor> <dx> <dy> <dz> <positive cmnd> <negative cmnd> <tool tip>
		else if(TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_manip_command_axis",false))
		{
			cmd.cmd = attr_Manip_Command_Axis;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			manip.axis[0] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.axis[1] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.axis[2] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref2);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=true;
			outObj.manips.push_back(manip);
		}
		// ATTR_manip_noop
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_manip_noop",true))
		{
			cmd.cmd = attr_Manip_Noop;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;		
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=true;
			outObj.manips.push_back(manip);			
		}
/******************************************************************************************************************************/
		// LIGHT LEVEL (930)
/******************************************************************************************************************************/
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_light_level",false))
		{
			cmd.cmd = attr_Light_Level;
			cmd.params[0] = TXT_MAP_flt_scan(cur_ptr,end_ptr,false);
			cmd.params[1] = TXT_MAP_flt_scan(cur_ptr,end_ptr,false);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&cmd.name);
			outObj.lods.back().cmds.push_back(cmd);
		}

		// ATTR_manip_push <cursor> <v1max> <v1min> <dref1> <tooltip>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_manip_push", false))
		{
			cmd.cmd = attr_Manip_Push;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			manip.v1_max = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v1_min = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=true;
			outObj.manips.push_back(manip);
		}
		// ATTR_manip_radio <cursor> <v1max> <dref1> <tooltip>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_manip_radio", false))
		{
			cmd.cmd = attr_Manip_Radio;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			manip.v1_max = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=true;
			outObj.manips.push_back(manip);
		}
		// ATTR_manip_toggle <cursor> <v1max> <v1min> <dref1> <tooltip>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_manip_toggle", false))
		{
			cmd.cmd = attr_Manip_Toggle;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			manip.v1_max = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v1_min = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=true;
			outObj.manips.push_back(manip);
		}

		// ATTR_manip_delta <cursor> <v1max> <v1min> <dref1> <tooltip>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_manip_delta", false))
		{
			cmd.cmd = attr_Manip_Delta;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			manip.v1_min = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v1_max = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v2_min = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v2_max = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=true;
			outObj.manips.push_back(manip);
		}
		// ATTR_manip_wrap <cursor> <v1max> <v1min> <dref1> <tooltip>
		else if (TXT_MAP_str_match_space(cur_ptr,end_ptr,"ATTR_manip_wrap", false))
		{
			cmd.cmd = attr_Manip_Wrap;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			manip.v1_min = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v1_max = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v2_min = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v2_max = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=true;
			outObj.manips.push_back(manip);
		}
/******************************************************************************************************************************/
		// NEW MANIPS (1050)
/******************************************************************************************************************************/
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_manip_wheel", false))
		{
			if(!outObj.manips.empty())
				outObj.manips.back().mouse_wheel_delta = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
		}
		// ATTR_manip_drag_axis_pix <cursor> <dx_pix> <step> <exp> <v1> <v2> <dataref> <tooltip>
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_manip_drag_axis_pix", xfals))
		{
			cmd.cmd = attr_Manip_Drag_Axis_Pix;;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);

			manip.axis[0] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.axis[1] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.axis[2] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);

			manip.v1_min = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v1_max = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);

			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);

			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=true;
			outObj.manips.push_back(manip);
		}
		// ATTR_manip_command_knob <cursor> <positive cmnd> <negative cmnd> <tool tip>
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_manip_command_knob", xfals))
		{
			cmd.cmd = attr_Manip_Command_Knob;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref2);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=xtrue;
			outObj.manips.push_back(manip);
		}
		// ATTR_manip_command_switch_up_down <cursor> <positive cmnd> <negative cmnd> <tool tip>
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_manip_command_switch_up_down", xfals))
		{
			cmd.cmd = attr_Manip_Command_Switch_Up_Down;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref2);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=xtrue;
			outObj.manips.push_back(manip);
		}
		// ATTR_manip_command_switch_left_right <cursor> <positive cmnd> <negative cmnd> <tool tip>
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_manip_command_switch_left_right", xfals))
		{
			cmd.cmd = attr_Manip_Command_Switch_Left_Right;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref2);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=xtrue;
			outObj.manips.push_back(manip);
		}

		// ATTR_manip_axis_switch_left_right <cursor>  <v1> <v2> <click step> <hold step> <dref> <tool tip>
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_manip_axis_knob", xfals))
		{
			cmd.cmd = attr_Manip_Axis_Knob;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);

			manip.v1_min = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v1_max = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.axis[0] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.axis[1] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);

			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=xtrue;
			outObj.manips.push_back(manip);
		}
		// ATTR_manip_axis_switch_up_down <cursor>  <v1> <v2> <click step> <hold step> <dref> <tool tip>
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_manip_axis_switch_up_down", xfals))
		{
			cmd.cmd = attr_Manip_Axis_Switch_Up_Down;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);

			manip.v1_min = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v1_max = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.axis[0] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.axis[1] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);

			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=xtrue;
			outObj.manips.push_back(manip);
		}
		// ATTR_manip_axis_switch_left_right <cursor>  <v1> <v2> <click step> <hold step> <dref> <tool tip>
		else if (TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_manip_axis_switch_left_right", xfals))
		{
			cmd.cmd = attr_Manip_Axis_Switch_Left_Right;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);

			manip.v1_min = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.v1_max = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.axis[0] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			manip.axis[1] = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);

			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=xtrue;
			outObj.manips.push_back(manip);
		}
/******************************************************************************************************************************/
		// PARTICLE SYSTEM
/******************************************************************************************************************************/
		// PARTICLE_SYSTEM <def name>
		else if(TXT_MAP_str_match_space(cur_ptr, end_ptr, "PARTICLE_SYSTEM",xfals))
		{
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &outObj.particle_system);
		}
		// EMITTER name x y z psi the phi low high dref
		else if(TXT_MAP_str_match_space(cur_ptr, end_ptr, "EMITTER",xfals))
		{
			cmd.cmd = attr_Emitter;
			cmd.idx_offset = outObj.emitters.size();
			outObj.lods.back().cmds.push_back(cmd);
		
			XObjEmitter8	em;
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &em.name);

			em.x = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			em.y = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			em.z = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);

			em.psi = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			em.the = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			em.phi = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);

			em.v_min = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);
			em.v_max = TXT_MAP_flt_scan(cur_ptr,end_ptr,xfals);

			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &em.dataref);

			outObj.emitters.push_back(em);
		}
/******************************************************************************************************************************/
		// V11 NEW STUFF
/******************************************************************************************************************************/
		// ATTR_cockpit_device <device> <bus> <rheostat> <auto_adjust>
		else if(TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_cockpit_device", xfals))
		{
			cmd.cmd = attr_Cockpit_Device;
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &cmd.name);
			cmd.params[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			cmd.params[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			cmd.params[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			outObj.lods.back().cmds.push_back(cmd);
		}
		else if(TXT_MAP_str_match_space(cur_ptr, end_ptr, "NORMAL_METALNESS",xtrue))
		{
			outObj.use_metalness = 1;
		}
		else if(TXT_MAP_str_match_space(cur_ptr, end_ptr, "BLEND_GLASS",xtrue))
		{
			outObj.glass_blending = 1;
		}
		// ATTR_axis_detented <dx> <dy> <dz> <v1_min> <v1_max> <dref>
		else if(TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_axis_detented", false))
		{
			XObjManip8& manip(outObj.manips.back());
			
			manip.centroid[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);	// centroid gets orthgonal lift axis
			manip.centroid[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			manip.centroid[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
		
			manip.v2_min = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			manip.v2_max = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref2);
		}
		// ATTR_manip_drag_rotate <cursor> <x> <y> <z> <dx> <dy> <dz> <ange1> <angle2> <lift> <v1min> <v1max> <v2min> <v2max> <dataref1> <dataref2> <tooltip>
		else if(TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_manip_drag_rotate", xfals))
		{
			cmd.cmd = attr_Manip_Drag_Rotate;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;

			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			
			manip.centroid[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			manip.centroid[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			manip.centroid[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);

			manip.axis[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			manip.axis[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			manip.axis[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);

			manip.angle_min = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			manip.angle_max = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			manip.lift		= TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			
			manip.v1_min = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			manip.v1_max = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			manip.v2_min = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			manip.v2_max = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref2);
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=true;
			outObj.manips.push_back(manip);
		}
		// ATTR_manip_keyframe dref angle
		else if(TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_manip_keyframe",xfals))
		{
			XObjKey k;
			k.key = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			k.v[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			outObj.manips.back().rotation_key_frames.push_back(k);
		}
		// ATTR_axis_detent_range <lo> <hi> <height>
		else if(TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_axis_detent_range", false))
		{
			XObjDetentRange d;
			d.lo = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			d.hi = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			d.height = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			outObj.manips.back().detents.push_back(d);
		}
		// ATTR_manip_command_switch_left_right2 <currsor> <cmnd> <tooltip>
		else if(TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_manip_command_switch_left_right2", xfals))
		{
			cmd.cmd = attr_Manip_Command_Switch_Left_Right2;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			manip.dataref2 = manip.dataref1;
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=true;
			outObj.manips.push_back(manip);
		}
		// ATTR_manip_command_switch_up_down2 <cursor> <cmnd> <tooltip>
		else if(TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_manip_command_switch_up_down2", xfals))
		{
			cmd.cmd = attr_Manip_Command_Switch_Up_Down2;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			manip.dataref2 = manip.dataref1;
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=true;
			outObj.manips.push_back(manip);
		}
		// ATTR_manip_command_knob2 <currsor> <cmnd> <tooltip>
		else if(TXT_MAP_str_match_space(cur_ptr, end_ptr, "ATTR_manip_command_knob2", xfals))
		{
			cmd.cmd = attr_Manip_Command_Knob2;
			cmd.idx_offset = outObj.manips.size();
			outObj.lods.back().cmds.push_back(cmd);
			XObjManip8 manip;
			manip.mouse_wheel_delta = 0.0f;
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.cursor);
			TXT_MAP_str_scan_space(cur_ptr,end_ptr,&manip.dataref1);
			manip.dataref2 = manip.dataref1;
			TXT_MAP_str_scan_eoln(cur_ptr,end_ptr,&manip.tooltip);
			ate_eoln=true;
			outObj.manips.push_back(manip);
		}
		else if(TXT_MAP_str_match_space(cur_ptr, end_ptr, "MAGNET", xfals))
		{
			cmd.cmd = attr_Magnet;
			// SKIP magnet name - we always write 'magnet'
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &cmd.name);
			// Scan a second time to pick up type, e.g. ipad
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &cmd.name);
			
			cmd.params[0] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			cmd.params[1] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			cmd.params[2] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			cmd.params[3] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			cmd.params[4] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
			cmd.params[5] = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);

			outObj.lods.back().cmds.push_back(cmd);
		}
		else if(TXT_MAP_str_match_space(cur_ptr, end_ptr, "#fixed_heading", xfals))
		{
			outObj.fixed_heading = TXT_MAP_flt_scan(cur_ptr, end_ptr, xfals);
		}
/******************************************************************************************************************************/
		// DEFAULT
/******************************************************************************************************************************/
		else
		// Common attribute handling:
		{
			string	attr_name;
			TXT_MAP_str_scan_space(cur_ptr, end_ptr, &attr_name);
			int cmd_idx = FindObjCmd(attr_name.c_str(), true);
			if (cmd_idx != gCmdCount)
			{
				cmd.cmd = gCmds[cmd_idx].cmd_id;
				for (n = 0; n < gCmds[cmd_idx].elem_count; ++n)
				{
					cmd.params[n] = TXT_MAP_flt_scan(cur_ptr, end_ptr, false);
				}
				outObj.lods.back().cmds.push_back(cmd);
			}

		}

		if(!ate_eoln)
			TXT_MAP_str_scan_eoln(cur_ptr, end_ptr, NULL);
	} // While loop

	free(mem_buf);

	outObj.geo_tri.get_minmax(outObj.xyz_min,outObj.xyz_max);
	
	return true;
}

/****************************************************************************************
 * OBJ 8 WRITE
 ****************************************************************************************/
bool	XObj8Write(const char * inFile, const XObj8& outObj)
{
	int n;

	FILE * fi = fopen(inFile, "wb");
	if (fi == NULL) return false;
	const float * v;

	// HEADER
	fprintf(fi, "%c" CRLF "800" CRLF "OBJ" CRLF CRLF, APL ? 'A' : 'I');

	// TEXTURES
									fprintf(fi, "TEXTURE %s" CRLF, outObj.texture.c_str());
	if (!outObj.texture_lit.empty())fprintf(fi, "TEXTURE_LIT %s" CRLF, outObj.texture_lit.c_str());
	if (!outObj.texture_normal_map.empty())fprintf(fi, "TEXTURE_NORMAL %s" CRLF, outObj.texture_normal_map.c_str());

	if(outObj.use_metalness)
		fprintf(fi,"NORMAL_METALNESS" CRLF);
	if(outObj.glass_blending)
		fprintf(fi,"BLEND_GLASS" CRLF);


	if(!outObj.particle_system.empty())
	fprintf(fi,"PARTICLE_SYSTEM %s" CRLF, outObj.particle_system.c_str());

	// SUBREGIONS
	for (int r = 0; r < outObj.regions.size(); ++r)
	{
		fprintf(fi,"COCKPIT_REGION %d %d %d %d" CRLF,
			outObj.regions[r].left,
			outObj.regions[r].bottom,
			outObj.regions[r].right,
			outObj.regions[r].top);
	}

	// POINT POOLS

	fprintf(fi, "POINT_COUNTS %d %d %d %llu" CRLF, outObj.geo_tri.count(), outObj.geo_lines.count(), outObj.geo_lights.count(), (unsigned long long)outObj.indices.size());

	for (n = 0; n < outObj.geo_tri.count(); ++n)
	{
		v = outObj.geo_tri.get(n);
		fprintf(fi, "VT %f %f %f %f %f %f %f %f" CRLF, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
	}

	for (n = 0; n < outObj.geo_lines.count(); ++n)
	{
		v = outObj.geo_lines.get(n);
		fprintf(fi, "VLINE %f %f %f %f %f %f" CRLF, v[0], v[1], v[2], v[3], v[4], v[5]);
	}

	for (n = 0; n < outObj.geo_lights.count(); ++n)
	{
		v = outObj.geo_lights.get(n);
		fprintf(fi, "VLIGHT %f %f %f %f %f %f" CRLF, v[0], v[1], v[2], v[3], v[4], v[5]);
	}

	int extra = outObj.indices.size() % 10;
	int trans = outObj.indices.size() - extra;

	for (n = 0; n < (int)outObj.indices.size(); ++n)
	{
		if (n >= trans) fprintf(fi, "IDX");
		else if ((n % 10) == 0) fprintf(fi, "IDX10");
		fprintf(fi, " %d", outObj.indices[n]);
		if (n >= trans) fprintf(fi, CRLF);
		else if ((n % 10) == 9) fprintf(fi, CRLF);
	}

	// CMDS

	for (vector<XObjLOD8>::const_iterator lod = outObj.lods.begin(); lod != outObj.lods.end(); ++lod)
	{
		if (lod->lod_far != 0.0)
		{
			fprintf(fi, "ATTR_LOD %f %f" CRLF, lod->lod_near, lod->lod_far);
		}

		for (vector<XObjCmd8>::const_iterator cmd = lod->cmds.begin(); cmd != lod->cmds.end(); ++cmd)
		{
			switch(cmd->cmd) {
			case anim_Rotate:
				if (outObj.animation[cmd->idx_offset].keyframes.size() == 2)
					fprintf(fi, "ANIM_rotate %f %f %f %f %f %f %f %s" CRLF,
						outObj.animation[cmd->idx_offset].axis[0],
						outObj.animation[cmd->idx_offset].axis[1],
						outObj.animation[cmd->idx_offset].axis[2],
						outObj.animation[cmd->idx_offset].keyframes[0].v[0],
						outObj.animation[cmd->idx_offset].keyframes[1].v[0],
						outObj.animation[cmd->idx_offset].keyframes[0].key,
						outObj.animation[cmd->idx_offset].keyframes[1].key,
						outObj.animation[cmd->idx_offset].dataref.c_str());
				else
				{
					fprintf(fi, "ANIM_rotate_begin %f %f %f %s" CRLF,
						outObj.animation[cmd->idx_offset].axis[0],
						outObj.animation[cmd->idx_offset].axis[1],
						outObj.animation[cmd->idx_offset].axis[2],
						outObj.animation[cmd->idx_offset].dataref.c_str());
					for(n = 0; n < outObj.animation[cmd->idx_offset].keyframes.size(); ++n)
						fprintf(fi, "ANIM_rotate_key %f %f" CRLF,
							outObj.animation[cmd->idx_offset].keyframes[n].key,
							outObj.animation[cmd->idx_offset].keyframes[n].v[0]);
					fprintf(fi, "ANIM_rotate_end" CRLF);
				}
				if(outObj.animation[cmd->idx_offset].loop)
					fprintf(fi,"ANIM_keyframe_loop %f" CRLF, outObj.animation[cmd->idx_offset].loop);
				break;
			case anim_Translate:
				if (outObj.animation[cmd->idx_offset].keyframes.size() == 2)
					fprintf(fi, "ANIM_trans %f %f %f %f %f %f %f %f %s" CRLF,
						outObj.animation[cmd->idx_offset].keyframes[0].v[0],
						outObj.animation[cmd->idx_offset].keyframes[0].v[1],
						outObj.animation[cmd->idx_offset].keyframes[0].v[2],
						outObj.animation[cmd->idx_offset].keyframes[1].v[0],
						outObj.animation[cmd->idx_offset].keyframes[1].v[1],
						outObj.animation[cmd->idx_offset].keyframes[1].v[2],
						outObj.animation[cmd->idx_offset].keyframes[0].key,
						outObj.animation[cmd->idx_offset].keyframes[1].key,
						outObj.animation[cmd->idx_offset].dataref.c_str());
				else
				{
					fprintf(fi, "ANIM_trans_begin %s" CRLF,
						outObj.animation[cmd->idx_offset].dataref.c_str());
					for(n = 0; n < outObj.animation[cmd->idx_offset].keyframes.size(); ++n)
						fprintf(fi, "ANIM_trans_key %f %f %f %f" CRLF,
							outObj.animation[cmd->idx_offset].keyframes[n].key,
							outObj.animation[cmd->idx_offset].keyframes[n].v[0],
							outObj.animation[cmd->idx_offset].keyframes[n].v[1],
							outObj.animation[cmd->idx_offset].keyframes[n].v[2]);
					fprintf(fi, "ANIM_trans_end" CRLF);
				}
				if(outObj.animation[cmd->idx_offset].loop)
					fprintf(fi,"ANIM_keyframe_loop %f" CRLF, outObj.animation[cmd->idx_offset].loop);
				break;
			case obj8_Tris:
				fprintf(fi, "TRIS %d %d" CRLF, cmd->idx_offset, cmd->idx_count);
				break;
			case obj8_Lines:
				fprintf(fi, "LINES %d %d" CRLF, cmd->idx_offset, cmd->idx_count);
				break;
			case obj8_Lights:
				fprintf(fi, "LIGHTS %d %d" CRLF, cmd->idx_offset, cmd->idx_count);
				break;
			// OBJ 850 crap
			case obj8_LightCustom:
				fprintf(fi, "LIGHT_CUSTOM %f %f %f %f %f %f %f %f %f %f %f %f %s" CRLF,
					cmd->params[0], cmd->params[1 ], cmd->params[2 ],
					cmd->params[3], cmd->params[4 ], cmd->params[5 ],
					cmd->params[6], cmd->params[7 ], cmd->params[8 ],
					cmd->params[9], cmd->params[10], cmd->params[11], cmd->name.c_str());
				break;
			case obj8_LightNamed:
				if(cmd->idx_count == 0)
					fprintf(fi,"LIGHT_NAMED %s %f %f %f" CRLF, cmd->name.c_str(),
						cmd->params[0], cmd->params[1 ], cmd->params[2 ]);
				else
				{
					fprintf(fi,"LIGHT_PARAM %s %f %f %f", cmd->name.c_str(),
						cmd->params[0], cmd->params[1 ], cmd->params[2 ]);
					for(int p = 0; p < cmd->idx_count; ++p)
						fprintf(fi," %.2f",cmd->params[3+p]);
					fprintf(fi,CRLF);
				}
				break;
			case attr_Layer_Group:
				fprintf(fi,"ATTR_layer_group %s %d" CRLF, cmd->name.c_str(), (int) cmd->params[0]);
				break;
			case anim_Hide:
				if (outObj.animation[cmd->idx_offset].keyframes.size() == 2)
					fprintf(fi, "ANIM_hide %f %f %s" CRLF,
						outObj.animation[cmd->idx_offset].keyframes[0].key,
						outObj.animation[cmd->idx_offset].keyframes[1].key,
						outObj.animation[cmd->idx_offset].dataref.c_str());
				if(outObj.animation[cmd->idx_offset].loop)
					fprintf(fi,"ANIM_keyframe_loop %f" CRLF, outObj.animation[cmd->idx_offset].loop);
				break;
			case anim_Show:
				if (outObj.animation[cmd->idx_offset].keyframes.size() == 2)
					fprintf(fi, "ANIM_show %f %f %s" CRLF,
						outObj.animation[cmd->idx_offset].keyframes[0].key,
						outObj.animation[cmd->idx_offset].keyframes[1].key,
						outObj.animation[cmd->idx_offset].dataref.c_str());
				if(outObj.animation[cmd->idx_offset].loop)
					fprintf(fi,"ANIM_keyframe_loop %f" CRLF, outObj.animation[cmd->idx_offset].loop);
				break;
			case attr_Hard:
				if (cmd->name == "object")
					fprintf(fi,"ATTR_hard" CRLF);
				else
					fprintf(fi,"ATTR_hard %s" CRLF, cmd->name.c_str());
				break;
			case attr_Hard_Deck:
				if (cmd->name == "object")
					fprintf(fi,"ATTR_hard_deck" CRLF);
				else
					fprintf(fi,"ATTR_hard_deck %s" CRLF, cmd->name.c_str());
				break;
			case attr_No_Blend:
				if (cmd->params[0] == 0.5)
					fprintf(fi,"ATTR_no_blend" CRLF);
				else
					fprintf(fi,"ATTR_no_blend %f" CRLF, cmd->params[0]);
				break;
			case attr_Tex_Cockpit_Subregion:
				fprintf(fi,"ATTR_cockpit_region %d" CRLF, (int) cmd->params[0]);
				break;

			case attr_Manip_Drag_2d:
				fprintf(fi,"ATTR_manip_drag_xy %s %f %f %f %f %f %f %s %s %s" CRLF,
					outObj.manips[cmd->idx_offset].cursor.c_str(),
					outObj.manips[cmd->idx_offset].axis[0],
					outObj.manips[cmd->idx_offset].axis[1],
					outObj.manips[cmd->idx_offset].v1_min,
					outObj.manips[cmd->idx_offset].v1_max,
					outObj.manips[cmd->idx_offset].v2_min,
					outObj.manips[cmd->idx_offset].v2_max,
					dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
					dataref_or_none(outObj.manips[cmd->idx_offset].dataref2.c_str()),
					outObj.manips[cmd->idx_offset].tooltip.c_str());
				break;
			case attr_Manip_Drag_Axis:
				fprintf(fi,"ATTR_manip_drag_axis %s %f %f %f %f %f %s %s" CRLF,
					outObj.manips[cmd->idx_offset].cursor.c_str(),
					outObj.manips[cmd->idx_offset].axis[0],
					outObj.manips[cmd->idx_offset].axis[1],
					outObj.manips[cmd->idx_offset].axis[2],
					outObj.manips[cmd->idx_offset].v1_min,
					outObj.manips[cmd->idx_offset].v1_max,
					dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
					outObj.manips[cmd->idx_offset].tooltip.c_str());

				if(!outObj.manips[cmd->idx_offset].dataref2.empty())
				{
					fprintf(fi,"ATTR_axis_detented %f %f %f\t%f %f %s" CRLF,
					outObj.manips[cmd->idx_offset].centroid[0],
					outObj.manips[cmd->idx_offset].centroid[1],
					outObj.manips[cmd->idx_offset].centroid[2],
					outObj.manips[cmd->idx_offset].v2_min,
					outObj.manips[cmd->idx_offset].v2_max,
					dataref_or_none(outObj.manips[cmd->idx_offset].dataref2.c_str()));
				}
				for(vector<XObjDetentRange>::const_iterator di = outObj.manips[cmd->idx_offset].detents.begin();
					di != outObj.manips[cmd->idx_offset].detents.end(); ++di)
				{
					fprintf(fi,"ATTR_axis_detent_range %f %f %f" CRLF, di->lo, di->hi, di->height);
				}
					
				if(outObj.manips[cmd->idx_offset].mouse_wheel_delta != 0)
					fprintf(fi,"ATTR_manip_wheel %f" CRLF, outObj.manips[cmd->idx_offset].mouse_wheel_delta);
				break;
			case attr_Manip_Command:
				fprintf(fi,"ATTR_manip_command %s %s %s" CRLF,
					outObj.manips[cmd->idx_offset].cursor.c_str(),
					dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
					outObj.manips[cmd->idx_offset].tooltip.c_str());
				break;
			case attr_Manip_Command_Axis:
				fprintf(fi,"ATTR_manip_command_axis %s %f %f %f %s %s %s" CRLF,
					outObj.manips[cmd->idx_offset].cursor.c_str(),
					outObj.manips[cmd->idx_offset].axis[0],
					outObj.manips[cmd->idx_offset].axis[1],
					outObj.manips[cmd->idx_offset].axis[2],
					dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
					dataref_or_none(outObj.manips[cmd->idx_offset].dataref2.c_str()),
					outObj.manips[cmd->idx_offset].tooltip.c_str());
				break;
			case attr_Manip_Noop:
				fprintf(fi,"ATTR_manip_noop %s %s" CRLF,
					dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
					outObj.manips[cmd->idx_offset].tooltip.c_str());
				break;
			case attr_Light_Level:
				fprintf(fi,"ATTR_light_level %f %f %s" CRLF, cmd->params[0], cmd->params[1],cmd->name.c_str());
				break;
				
			case attr_Manip_Push:
					fprintf(fi,"ATTR_manip_push %s %f %f %s %s" CRLF,
						outObj.manips[cmd->idx_offset].cursor.c_str(),
						outObj.manips[cmd->idx_offset].v1_max,
						outObj.manips[cmd->idx_offset].v1_min,
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
						outObj.manips[cmd->idx_offset].tooltip.c_str());
					if(outObj.manips[cmd->idx_offset].mouse_wheel_delta != 0)
						fprintf(fi,"ATTR_manip_wheel %f" CRLF, outObj.manips[cmd->idx_offset].mouse_wheel_delta);
					break;
			case attr_Manip_Radio:
					fprintf(fi,"ATTR_manip_radio %s %f %s %s" CRLF,
						outObj.manips[cmd->idx_offset].cursor.c_str(),
						outObj.manips[cmd->idx_offset].v1_max,
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
						outObj.manips[cmd->idx_offset].tooltip.c_str());
					if(outObj.manips[cmd->idx_offset].mouse_wheel_delta != 0)
						fprintf(fi,"ATTR_manip_wheel %f" CRLF, outObj.manips[cmd->idx_offset].mouse_wheel_delta);
					break;
			case attr_Manip_Toggle:
					fprintf(fi,"ATTR_manip_toggle %s %f %f %s %s" CRLF,
						outObj.manips[cmd->idx_offset].cursor.c_str(),
						outObj.manips[cmd->idx_offset].v1_max,
						outObj.manips[cmd->idx_offset].v1_min,
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
						outObj.manips[cmd->idx_offset].tooltip.c_str());
					if(outObj.manips[cmd->idx_offset].mouse_wheel_delta != 0)
						fprintf(fi,"ATTR_manip_wheel %f" CRLF, outObj.manips[cmd->idx_offset].mouse_wheel_delta);
					break;
					
			case attr_Manip_Delta:
					fprintf(fi,"ATTR_manip_delta %s %f %f %f %f %s %s" CRLF,
						outObj.manips[cmd->idx_offset].cursor.c_str(),
						outObj.manips[cmd->idx_offset].v1_min,
						outObj.manips[cmd->idx_offset].v1_max,
						outObj.manips[cmd->idx_offset].v2_min,
						outObj.manips[cmd->idx_offset].v2_max,
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
						outObj.manips[cmd->idx_offset].tooltip.c_str());
					if(outObj.manips[cmd->idx_offset].mouse_wheel_delta != 0)
						fprintf(fi,"ATTR_manip_wheel %f" CRLF, outObj.manips[cmd->idx_offset].mouse_wheel_delta);
					break;

			case attr_Manip_Wrap:
					fprintf(fi,"ATTR_manip_wrap %s %f %f %f %f %s %s" CRLF,
						outObj.manips[cmd->idx_offset].cursor.c_str(),
						outObj.manips[cmd->idx_offset].v1_min,
						outObj.manips[cmd->idx_offset].v1_max,
						outObj.manips[cmd->idx_offset].v2_min,
						outObj.manips[cmd->idx_offset].v2_max,
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
						outObj.manips[cmd->idx_offset].tooltip.c_str());
					if(outObj.manips[cmd->idx_offset].mouse_wheel_delta != 0)
						fprintf(fi,"ATTR_manip_wheel %f" CRLF, outObj.manips[cmd->idx_offset].mouse_wheel_delta);
					break;
			case attr_Manip_Drag_Axis_Pix:
					fprintf(fi,"ATTR_manip_drag_axis_pix %s %f %f %f %f %f %s %s" CRLF,
						outObj.manips[cmd->idx_offset].cursor.c_str(),
						outObj.manips[cmd->idx_offset].axis[0],
						outObj.manips[cmd->idx_offset].axis[1],
						outObj.manips[cmd->idx_offset].axis[2],
						outObj.manips[cmd->idx_offset].v1_min,
						outObj.manips[cmd->idx_offset].v1_max,
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
						outObj.manips[cmd->idx_offset].tooltip.c_str());
					if(outObj.manips[cmd->idx_offset].mouse_wheel_delta != 0)
						fprintf(fi,"ATTR_manip_wheel %f" CRLF, outObj.manips[cmd->idx_offset].mouse_wheel_delta);
					break;		
			case attr_Manip_Command_Knob:
					fprintf(fi,"ATTR_manip_command_knob %s %s %s %s" CRLF,
						outObj.manips[cmd->idx_offset].cursor.c_str(),
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref2.c_str()),
						outObj.manips[cmd->idx_offset].tooltip.c_str());
					break;
			case attr_Manip_Command_Switch_Up_Down:
					fprintf(fi,"ATTR_manip_command_switch_up_down %s %s %s %s" CRLF,
						outObj.manips[cmd->idx_offset].cursor.c_str(),
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref2.c_str()),
						outObj.manips[cmd->idx_offset].tooltip.c_str());
					break;
			case attr_Manip_Command_Switch_Left_Right:
					fprintf(fi,"ATTR_manip_command_switch_left_right %s %s %s %s" CRLF,
						outObj.manips[cmd->idx_offset].cursor.c_str(),
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref2.c_str()),
						outObj.manips[cmd->idx_offset].tooltip.c_str());
					break;
			case attr_Manip_Axis_Knob:
					fprintf(fi,"ATTR_manip_axis_knob %s %f %f %f %f %s %s" CRLF,
						outObj.manips[cmd->idx_offset].cursor.c_str(),
						outObj.manips[cmd->idx_offset].v1_min,
						outObj.manips[cmd->idx_offset].v1_max,
						outObj.manips[cmd->idx_offset].axis[0],
						outObj.manips[cmd->idx_offset].axis[1],
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
						outObj.manips[cmd->idx_offset].tooltip.c_str());
					break;
			case attr_Manip_Axis_Switch_Up_Down:
					fprintf(fi,"ATTR_manip_axis_switch_up_down %s %f %f %f %f %s %s" CRLF,
						outObj.manips[cmd->idx_offset].cursor.c_str(),
						outObj.manips[cmd->idx_offset].v1_min,
						outObj.manips[cmd->idx_offset].v1_max,
						outObj.manips[cmd->idx_offset].axis[0],
						outObj.manips[cmd->idx_offset].axis[1],
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
						outObj.manips[cmd->idx_offset].tooltip.c_str());
					break;
			case attr_Manip_Axis_Switch_Left_Right:
					fprintf(fi,"ATTR_manip_axis_switch_left_right %s %f %f %f %f %s %s" CRLF,
						outObj.manips[cmd->idx_offset].cursor.c_str(),
						outObj.manips[cmd->idx_offset].v1_min,
						outObj.manips[cmd->idx_offset].v1_max,
						outObj.manips[cmd->idx_offset].axis[0],
						outObj.manips[cmd->idx_offset].axis[1],
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
						outObj.manips[cmd->idx_offset].tooltip.c_str());
					break;
			case attr_Emitter:
					fprintf(fi,"EMITTER %s %f %f %f %f %f %f %f %f %s" CRLF,
						outObj.emitters[cmd->idx_offset].name.c_str(),
						outObj.emitters[cmd->idx_offset].x,
						outObj.emitters[cmd->idx_offset].y,
						outObj.emitters[cmd->idx_offset].z,
						outObj.emitters[cmd->idx_offset].psi,
						outObj.emitters[cmd->idx_offset].the,
						outObj.emitters[cmd->idx_offset].phi,
						outObj.emitters[cmd->idx_offset].v_min,
						outObj.emitters[cmd->idx_offset].v_max,
						outObj.emitters[cmd->idx_offset].dataref.c_str());
					break;
			case attr_Cockpit_Device:
					fprintf(fi,"ATTR_cockpit_device %s %d %d %d" CRLF,
						cmd->name.c_str(),
						(int) cmd->params[0],
						(int) cmd->params[1],
						(int) cmd->params[2]);
					break;
			case attr_Manip_Drag_Rotate:
					fprintf(fi,"ATTR_manip_drag_rotate %s %f %f %f\t%f %f %f\t%f %f %f\t%f %f  %f %f %s %s %s" CRLF,
						outObj.manips[cmd->idx_offset].cursor.c_str(),
						outObj.manips[cmd->idx_offset].centroid[0],
						outObj.manips[cmd->idx_offset].centroid[1],
						outObj.manips[cmd->idx_offset].centroid[2],
						outObj.manips[cmd->idx_offset].axis[0],
						outObj.manips[cmd->idx_offset].axis[1],
						outObj.manips[cmd->idx_offset].axis[2],
						outObj.manips[cmd->idx_offset].angle_min,
						outObj.manips[cmd->idx_offset].angle_max,
						outObj.manips[cmd->idx_offset].lift,
						outObj.manips[cmd->idx_offset].v1_min,
						outObj.manips[cmd->idx_offset].v1_max,
						outObj.manips[cmd->idx_offset].v2_min,
						outObj.manips[cmd->idx_offset].v2_max,
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
						dataref_or_none(outObj.manips[cmd->idx_offset].dataref2.c_str()),
						outObj.manips[cmd->idx_offset].tooltip.c_str());
					
					for(vector<XObjKey>::const_iterator ki = outObj.manips[cmd->idx_offset].rotation_key_frames.begin();
						ki != outObj.manips[cmd->idx_offset].rotation_key_frames.end(); ++ki)
					{
						fprintf(fi,"ATTR_manip_keyframe %f %f" CRLF, ki->key, ki->v[0]);
					}
					for(vector<XObjDetentRange>::const_iterator di = outObj.manips[cmd->idx_offset].detents.begin();
						di != outObj.manips[cmd->idx_offset].detents.end(); ++di)
					{
						fprintf(fi,"ATTR_axis_detent_range %f %f %f" CRLF, di->lo, di->hi, di->height);
					}
					if(outObj.manips[cmd->idx_offset].mouse_wheel_delta != 0)
						fprintf(fi,"ATTR_manip_wheel %f" CRLF, outObj.manips[cmd->idx_offset].mouse_wheel_delta);
					
					break;
			case attr_Manip_Command_Switch_Left_Right2:
				fprintf(fi,"ATTR_manip_command_switch_left_right2 %s %s %s" CRLF,
					outObj.manips[cmd->idx_offset].cursor.c_str(),
					dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
					outObj.manips[cmd->idx_offset].tooltip.c_str());
				break;
			case attr_Manip_Command_Switch_Up_Down2:
				fprintf(fi,"ATTR_manip_command_switch_up_down2 %s %s %s" CRLF,
					outObj.manips[cmd->idx_offset].cursor.c_str(),
					dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
					outObj.manips[cmd->idx_offset].tooltip.c_str());
				break;
			case attr_Manip_Command_Knob2:
				fprintf(fi,"ATTR_manip_command_knob2 %s %s %s" CRLF,
					outObj.manips[cmd->idx_offset].cursor.c_str(),
					dataref_or_none(outObj.manips[cmd->idx_offset].dataref1.c_str()),
					outObj.manips[cmd->idx_offset].tooltip.c_str());
				break;
			case attr_Magnet:
				fprintf(fi,"MAGNET magnet %s %f %f %f  %f %f %f" CRLF,
					cmd->name.c_str(),
					cmd->params[0],
					cmd->params[1],
					cmd->params[2],

					cmd->params[3],
					cmd->params[4],
					cmd->params[5]);
				break;
			default:
				{
					int idx = FindIndexForCmd(cmd->cmd);
					if (idx != gCmdCount)
					{
						fprintf(fi, "%s", gCmds[idx].name);
						for (n = 0; n < gCmds[idx].elem_count; ++n)
							fprintf(fi, " %f", cmd->params[n]);
						fprintf(fi, CRLF);
					}
				}
				break;
			}
		}
	}
	fclose(fi);
	return true;
}
