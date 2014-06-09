/*=========================================================================

Program:   Visualization Toolkit
Module:    vtkOpenGL2Renderer.cxx

Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
All rights reserved.
See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

This software is distributed WITHOUT ANY WARRANTY; without even
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkOpenGL2Renderer.h"

#include "vtkNew.h"
#include "vtkPolyDataMapper2D.h"
#include "vtkPoints.h"
#include "vtkUnsignedCharArray.h"
#include "vtkFloatArray.h"
#include "vtkPolyData.h"
#include "vtkPointData.h"
#include "vtkCellArray.h"
#include "vtkTrivialProducer.h"
#include "vtkTexturedActor2D.h"

#include "vtkglVBOHelper.h"

#include "vtkCuller.h"
#include "vtkLightCollection.h"
#include "vtkObjectFactory.h"
#include "vtkOpenGLCamera.h"
#include "vtkLight.h"
#include "vtkOpenGLProperty.h"
#include "vtkRenderWindow.h"
#include "vtkOpenGLRenderWindow.h"
#include "vtkOpenGLExtensionManager.h"
#include "vtkgl.h" // vtkgl namespace
#include "vtkOpenGLTexture.h"
#include "vtkTimerLog.h"
#include "vtkRenderPass.h"
#include "vtkRenderState.h"

#include "vtkOpenGL.h"
#include "vtkOpenGLError.h"

#include <math.h>
#include <cassert>
#include <list>


// not used
#include "vtkImageImport.h"
#include "vtkPNGWriter.h"

class vtkGLPickInfo
{
public:
  GLuint* PickBuffer;
  GLuint PickedId;
  GLuint NumPicked;

  GLuint PickingFBO;
  GLuint PickingTexture;
  GLuint DepthTexture;
};

vtkStandardNewMacro(vtkOpenGL2Renderer);

vtkCxxSetObjectMacro(vtkOpenGL2Renderer, Pass, vtkRenderPass);

// List of rgba layers, id are 2D rectangle texture Ids.
class vtkOpenGL2RendererLayerList
{
public:
  std::list<GLuint> List;
};

vtkOpenGL2Renderer::vtkOpenGL2Renderer()
{
  this->PickInfo = new vtkGLPickInfo;
  this->PickInfo->PickBuffer = 0;
  this->PickInfo->PickedId = 0;
  this->PickInfo->NumPicked = 0;
  this->PickedZ = 0;

  this->DepthPeelingIsSupported=0;
  this->DepthPeelingIsSupportedChecked=0;
  this->LayerList=0;
  this->OpaqueLayerZ=0;
  this->TransparentLayerZ=0;
  this->DepthFormat=0;
  this->DepthPeelingHigherLayer=0;

  this->BackgroundTexture = 0;
  this->Pass = 0;
}

// Ask lights to load themselves into graphics pipeline.
int vtkOpenGL2Renderer::UpdateLights ()
{
  vtkOpenGLClearErrorMacro();

  vtkLight *light;
  float status;
  int count = 0;

  vtkCollectionSimpleIterator sit;
  for(this->Lights->InitTraversal(sit);
      (light = this->Lights->GetNextLight(sit)); )
    {
    status = light->GetSwitch();
    if (status > 0.0)
      {
      count++;
      }
    }

  if( !count )
    {
    vtkDebugMacro(<<"No lights are on, creating one.");
    this->CreateLight();
    }

  for(this->Lights->InitTraversal(sit);
      (light = this->Lights->GetNextLight(sit)); )
    {
    status = light->GetSwitch();

    // if the light is on then define it and bind it.
    if (status > 0.0)
      {
      light->Render(this,0);
      }
    }

  vtkOpenGLCheckErrorMacro("failed after UpdateLights");

  return count;
}

// ----------------------------------------------------------------------------
// Description:
// Is rendering at translucent geometry stage using depth peeling and
// rendering a layer other than the first one? (Boolean value)
// If so, the uniform variables UseTexture and Texture can be set.
// (Used by vtkOpenGLProperty or vtkOpenGLTexture)
int vtkOpenGL2Renderer::GetDepthPeelingHigherLayer()
{
  return this->DepthPeelingHigherLayer;
}

// ----------------------------------------------------------------------------
// Concrete open gl render method.
void vtkOpenGL2Renderer::DeviceRender(void)
{
  vtkTimerLog::MarkStartEvent("OpenGL Dev Render");

  if(this->Pass!=0)
    {
    vtkRenderState s(this);
    s.SetPropArrayAndCount(this->PropArray, this->PropArrayCount);
    s.SetFrameBuffer(0);
    this->Pass->Render(&s);
    }
  else
    {
    // Do not remove this MakeCurrent! Due to Start / End methods on
    // some objects which get executed during a pipeline update,
    // other windows might get rendered since the last time
    // a MakeCurrent was called.
    this->RenderWindow->MakeCurrent();
    vtkOpenGLClearErrorMacro();

    this->UpdateCamera();
    this->UpdateLightGeometry();
    this->UpdateLights();
    this->UpdateGeometry();

    vtkOpenGLCheckErrorMacro("failed after DeviceRender");
    }

  vtkTimerLog::MarkEndEvent("OpenGL Dev Render");
}

// ----------------------------------------------------------------------------
// Description:
// Render translucent polygonal geometry. Default implementation just call
// UpdateTranslucentPolygonalGeometry().
// Subclasses of vtkRenderer that can deal with depth peeling must
// override this method.
void vtkOpenGL2Renderer::DeviceRenderTranslucentPolygonalGeometry()
{
  vtkOpenGLClearErrorMacro();

  if(this->UseDepthPeeling)
    {
    }

  if(!this->UseDepthPeeling || !this->DepthPeelingIsSupported)
    {
    // just alpha blending
    this->LastRenderingUsedDepthPeeling=0;
    this->UpdateTranslucentPolygonalGeometry();
    }
  else
    {
    // depth peeling.

    }

  vtkOpenGLCheckErrorMacro("failed after DeviceRenderTranslucentPolygonalGeometry");
}

// ----------------------------------------------------------------------------
void vtkOpenGL2Renderer::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);

  os << indent << "PickBuffer " << this->PickInfo->PickBuffer << "\n";
  os << indent << "PickedId" << this->PickInfo->PickedId<< "\n";
  os << indent << "NumPicked" << this->PickInfo->NumPicked<< "\n";
  os << indent << "PickedZ " << this->PickedZ << "\n";
  os << indent << "Pass:";
  if(this->Pass!=0)
    {
      os << "exists" << endl;
    }
  else
    {
      os << "null" << endl;
    }
}


void vtkOpenGL2Renderer::Clear(void)
{
  vtkOpenGLClearErrorMacro();

  GLbitfield  clear_mask = 0;

  if (! this->Transparent())
    {
    glClearColor( static_cast<GLclampf>(this->Background[0]),
                  static_cast<GLclampf>(this->Background[1]),
                  static_cast<GLclampf>(this->Background[2]),
                  static_cast<GLclampf>(0.0));
    clear_mask |= GL_COLOR_BUFFER_BIT;
    }

  vtkDebugMacro(<< "glClear\n");
  glClear(clear_mask);

  // If gradient background is turned on, draw it now.
  if (!this->IsPicking && !this->Transparent() &&
      (this->GradientBackground || this->TexturedBackground))
    {
    int size[2];
    size[0] = this->GetSize()[0];
    size[1] = this->GetSize()[1];

    double tile_viewport[4];
    this->GetRenderWindow()->GetTileViewport(tile_viewport);

    vtkNew<vtkTexturedActor2D> actor;
    vtkNew<vtkPolyDataMapper2D> mapper;
    vtkNew<vtkPolyData> polydata;
    vtkNew<vtkPoints> points;
    points->SetNumberOfPoints(4);
    points->SetPoint(0, 0, 0, 0);
    points->SetPoint(1, size[0], 0, 0);
    points->SetPoint(2, size[0], size[1], 0);
    points->SetPoint(3, 0, size[1], 0);
	  polydata->SetPoints(points.Get());

    vtkNew<vtkCellArray> tris;
    tris->InsertNextCell(3);
    tris->InsertCellPoint(0);
    tris->InsertCellPoint(1);
    tris->InsertCellPoint(2);
    tris->InsertNextCell(3);
    tris->InsertCellPoint(0);
    tris->InsertCellPoint(2);
    tris->InsertCellPoint(3);
    polydata->SetPolys(tris.Get());

    vtkNew<vtkTrivialProducer> prod;
    prod->SetOutput(polydata.Get());

    // Set some properties.
    mapper->SetInputConnection(prod->GetOutputPort());
    actor->SetMapper(mapper.Get());

    if(this->TexturedBackground && this->BackgroundTexture)
      {
      this->BackgroundTexture->InterpolateOn();
      actor->SetTexture(this->BackgroundTexture);

      vtkNew<vtkFloatArray> tcoords;
      float tmp[2];
      tmp[0] = 0;
      tmp[1] = 0;
      tcoords->SetNumberOfComponents(2);
      tcoords->SetNumberOfTuples(4);
      tcoords->SetTuple(0,tmp);
      tmp[0] = 1.0;
      tcoords->SetTuple(1,tmp);
      tmp[1] = 1.0;
      tcoords->SetTuple(2,tmp);
      tmp[0] = 0.0;
      tcoords->SetTuple(3,tmp);
      polydata->GetPointData()->SetTCoords(tcoords.Get());
      }
    else // gradient
      {
      vtkNew<vtkUnsignedCharArray> colors;
      float tmp[4];
      tmp[0] = this->Background[0]*255;
      tmp[1] = this->Background[1]*255;
      tmp[2] = this->Background[2]*255;
      tmp[3] = 255;
      colors->SetNumberOfComponents(4);
      colors->SetNumberOfTuples(4);
      colors->SetTuple(0,tmp);
      colors->SetTuple(1,tmp);
      tmp[0] = this->Background2[0]*255;
      tmp[1] = this->Background2[1]*255;
      tmp[2] = this->Background2[2]*255;
      colors->SetTuple(2,tmp);
      colors->SetTuple(3,tmp);
      polydata->GetPointData()->SetScalars(colors.Get());
      }

    glDisable(GL_DEPTH_TEST);
    actor->RenderOverlay(this);
    }

  if (!this->GetPreserveDepthBuffer())
    {
    glClearDepth(static_cast<GLclampf>(1.0));
    glClear(GL_DEPTH_BUFFER_BIT);
    }

  glEnable(GL_DEPTH_TEST);

  vtkOpenGLCheckErrorMacro("failed after Clear");
}

void vtkOpenGL2Renderer::StartPick(unsigned int vtkNotUsed(pickFromSize))
{
  vtkOpenGLClearErrorMacro();

  /*
  int size[2];
  size[0] = this->GetSize()[0];
  size[1] = this->GetSize()[1];

  // Create the FBO
  glGenFramebuffers(1, &this->PickInfo->PickingFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, this->PickInfo->PickingFBO);

  // Create the texture object for the primitive information buffer
  glGenTextures(1, &this->PickInfo->PickingTexture);
  glBindTexture(GL_TEXTURE_2D, this->PickInfo->PickingTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32UI, size[0], size[1],
              0, GL_RGB_INTEGER, GL_UNSIGNED_INT, NULL);
  glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
              this->PickInfo->PickingTexture, 0);

  // Create the texture object for the depth buffer
  glGenTextures(1, &this->PickInfo->DepthTexture);
  glBindTexture(GL_TEXTURE_2D, this->PickInfo->DepthTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, size[0], size[1],
              0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
  glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
              this->PickInfo->DepthTexture, 0);

  // Disable reading to avoid problems with older GPUs
  glReadBuffer(GL_NONE);

  // Verify that the FBO is correct
  GLenum Status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

  if (Status != GL_FRAMEBUFFER_COMPLETE)
    {
    printf("FB error, status: 0x%x\n", Status);
    return;
    }

  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, this->PickInfo->PickingFBO);
  */

  // Do not remove this MakeCurrent! Due to Start / End methods on
  // some objects which get executed during a pipeline update,
  // other windows might get rendered since the last time
  // a MakeCurrent was called.
  this->RenderWindow->MakeCurrent();
  this->RenderWindow->IsPickingOn();
  this->IsPicking = 1;
  this->Clear();

  vtkOpenGLCheckErrorMacro("failed after StartPick");
}

void vtkOpenGL2Renderer::ReleaseGraphicsResources(vtkWindow *w)
{
  if (w && this->Pass)
    {
    this->Pass->ReleaseGraphicsResources(w);
    }
}

void vtkOpenGL2Renderer::UpdatePickId()
{
  this->CurrentPickId++;
}


void vtkOpenGL2Renderer::DevicePickRender()
{
  // Do not remove this MakeCurrent! Due to Start / End methods on
  // some objects which get executed during a pipeline update,
  // other windows might get rendered since the last time
  // a MakeCurrent was called.
  this->RenderWindow->MakeCurrent();
  vtkOpenGLClearErrorMacro();

  this->UpdateCamera();
  this->UpdateLightGeometry();
  this->UpdateLights();

  this->PickGeometry();

  vtkOpenGLCheckErrorMacro("failed after DevicePickRender");
}


void vtkOpenGL2Renderer::DonePick()
{
  glFlush();

  std::map<unsigned int,float> depthValues;

  unsigned char *pixBuffer = this->GetRenderWindow()->GetPixelData(
    this->PickX1, this->PickY1, this->PickX2, this->PickY2, 0);
//    (this->GetRenderWindow()->GetSwapBuffers() == 1) ? 0 : 1);

  // for debugging save out the image
  FILE * pFile;
  pFile = fopen ("myfile.ppm", "wb");
  fwrite (pixBuffer , sizeof(unsigned char), 3*((int)this->PickY2-(int)this->PickY1+1)*((int)this->PickX2-(int)this->PickX1+1), pFile);
  fclose (pFile);

  float *depthBuffer = this->GetRenderWindow()->GetZbufferData(
    this->PickX1, this->PickY1, this->PickX2, this->PickY2);

  // read the color and z buffer values for the region
  // to see what hits we have
  unsigned char *pb = pixBuffer;
  float *dbPtr = depthBuffer;
  for (int y = this->PickY1; y <= this->PickY2; y++)
    {
    for (int x = this->PickX1; x <= this->PickX2; x++)
      {
      unsigned char rgb[3];
      rgb[0] = *pb++;
      rgb[1] = *pb++;
      rgb[2] = *pb++;
      int val = 0;
      val |= rgb[2];
      val = val << 8;
      val |= rgb[1];
      val = val << 8;
      val |= rgb[0];
      if (val > 0)
        {
        if (depthValues.find(val) == depthValues.end())
          {
          depthValues.insert(std::pair<unsigned int,float>(val,*dbPtr));
          }
        }
      dbPtr++;
      }
    }

  this->PickInfo->NumPicked = depthValues.size();

  this->PickInfo->PickedId = 0;
  std::map<unsigned int,float>::const_iterator dvItr =
    depthValues.begin();
  this->PickedZ = 1.0;
  for ( ; dvItr != depthValues.end(); dvItr++)
    {
    if(dvItr->second < this->PickedZ)
      {
      this->PickedZ = dvItr->second;
      this->PickInfo->PickedId = dvItr->first - 1;
      }
    }

  // Restore the default framebuffer
  //glBindFramebuffer(GL_FRAMEBUFFER, 0);

  this->RenderWindow->IsPickingOff();
  this->IsPicking = 0;
}

double vtkOpenGL2Renderer::GetPickedZ()
{
  return this->PickedZ;
}

unsigned int vtkOpenGL2Renderer::GetPickedId()
{
  return static_cast<unsigned int>(this->PickInfo->PickedId);
}

vtkOpenGL2Renderer::~vtkOpenGL2Renderer()
{
  if (this->PickInfo->PickBuffer)
    {
    delete [] this->PickInfo->PickBuffer;
    this->PickInfo->PickBuffer = 0;
    }
  delete this->PickInfo;

  if(this->Pass!=0)
    {
    this->Pass->UnRegister(this);
    }
}

unsigned int vtkOpenGL2Renderer::GetNumPickedIds()
{
  return static_cast<unsigned int>(this->PickInfo->NumPicked);
}

int vtkOpenGL2Renderer::GetPickedIds(unsigned int atMost,
                                    unsigned int *callerBuffer)
{
  if (!this->PickInfo->PickBuffer)
    {
    return 0;
    }

  unsigned int max = (atMost < this->PickInfo->NumPicked) ? atMost : this->PickInfo->NumPicked;
  GLuint* iptr = this->PickInfo->PickBuffer;
  unsigned int *optr = callerBuffer;
  unsigned int k;
  for(k =0; k < max; k++)
    {
    int num_names = *iptr;
    iptr++; // move to first depth value
    iptr++; // move to next depth value
    iptr++; // move to first name picked
    *optr = static_cast<unsigned int>(*iptr);
    optr++;
    // skip additional names
    iptr += num_names;
    }
  return k;
}
