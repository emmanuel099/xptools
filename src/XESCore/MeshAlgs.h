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
#ifndef MESHALGS_H
#define MESHALGS_H

#include "MeshDefs.h"
#include "ProgressUtils.h"
#include "CompGeomDefs3.h"


// Burn...everything.
#define BURN_EVERYTHING 0
// Burn every road segment!
#define BURN_ROADS 0

template<typename Number>
struct	PolyRasterizer;
struct	DEMGeo;
class	DEMGeoMap;
class	CDT;
//class	Pmwx;

struct	MeshPrefs_t {
	int		max_points;
	float	max_error;
	int		border_match;
	int		optimize_borders;
	float	max_tri_size_m;
	float	rep_switch_m;
};
extern MeshPrefs_t	gMeshPrefs;

void	TriangulateMesh(Pmwx& inMap, CDT& outMesh, DEMGeoMap& inDEMs, const char * mesh_folder, ProgressFunc inFunc);
void	AssignLandusesToMesh(	DEMGeoMap& inDems,
								CDT& ioMesh,
								const char * mesh_folder,
								ProgressFunc inProg);

void 	SetupWaterRasterizer(const Pmwx& inMap, const DEMGeo& inDEM, PolyRasterizer<double>& outRasterizer, int terrain_wanted);
double	HeightWithinTri(CDT& inMesh, CDT::Face_handle tri, CDT::Point in);
double	MeshHeightAtPoint(CDT& inMesh, double inLon, double inLat, int hint_id);
void	Calc2ndDerivative(DEMGeo& ioDEM);
int		CalcMeshError(CDT& mesh, DEMGeo& elev, float& out_min, float& out_max, float& out_ave, float& std_dev, ProgressFunc inFunc);
int		CalcMeshTextures(CDT& inMesh, map<int, int>& out_lus);


// This routine is inline because we need a semi-global definition of what a "real" edge of the pmwx is.

inline bool must_burn_he(Halfedge_handle he)
{
	#if BURN_EVERYTHING
		return true;
	#endif
	Halfedge_handle tw = he->twin();
	Face_handle f1 = he->face();
	Face_handle f2 = tw->face();
	
	if(f1->is_unbounded() || f2->is_unbounded()) 
		return false;

#if BURN_ROADS
	if(!he->data().mSegments.empty() ||
		!he->twin()->data().mSegments.empty()) return true;
#endif
	
	return he->data().mParams.count(he_MustBurn) ||
		   tw->data().mParams.count(he_MustBurn) ||
//		   f1->data().GetZoning() != f2->data().GetZoning() ||
#if !DEV
//	#error put zoning back
#endif
		   f1->data().mTerrainType != f2->data().mTerrainType;
}



#endif
