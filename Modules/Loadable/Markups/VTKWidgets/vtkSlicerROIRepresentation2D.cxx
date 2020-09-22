/*==============================================================================

  Copyright (c) Laboratory for Percutaneous Surgery (PerkLab)
  Queen's University, Kingston, ON, Canada. All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Kyle Sunderland, PerkLab, Queen's University
  and was supported through CANARIE's Research Software Program, Cancer
  Care Ontario, OpenAnatomy, and Brigham and Women’s Hospital through NIH grant R01MH112748.

==============================================================================*/

// VTK includes
#include <vtkActor2D.h>
#include <vtkArcSource.h>
#include <vtkAppendPolyData.h>
#include <vtkCellLocator.h>
#include <vtkClipPolyData.h>
#include <vtkCompositeDataGeometryFilter.h>
#include <vtkDiscretizableColorTransferFunction.h>
#include <vtkDoubleArray.h>
#include <vtkFeatureEdges.h>
#include <vtkGlyph2D.h>
#include <vtkLine.h>
#include <vtkMath.h>
#include <vtkMatrix4x4.h>
#include <vtkObjectFactory.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPlane.h>
#include <vtkPlaneCutter.h>
#include <vtkPlaneSource.h>
#include <vtkPoints.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkProperty2D.h>
#include <vtkRenderer.h>
#include <vtkSampleImplicitFunctionFilter.h>
#include <vtkSlicerROIRepresentation2D.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>

// MRML includes
#include "vtkMRMLInteractionEventData.h"
#include "vtkMRMLMarkupsDisplayNode.h"
#include "vtkMRMLProceduralColorNode.h"

#include "vtkMRMLMarkupsPlaneNode.h"

#include "vtkMarkupsGlyphSource2D.h"

vtkStandardNewMacro(vtkSlicerROIRepresentation2D);

#include <vtkCubeSource.h>

//----------------------------------------------------------------------
vtkSlicerROIRepresentation2D::vtkSlicerROIRepresentation2D()
{
  this->CubeMapper->SetInputConnection(this->CubeFilter->GetOutputPort());
  this->CubeActor->SetMapper(this->CubeMapper);
  this->CubeActor->SetProperty(this->GetControlPointsPipeline(Unselected)->Property);
}

//----------------------------------------------------------------------
vtkSlicerROIRepresentation2D::~vtkSlicerROIRepresentation2D() = default;

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::UpdateFromMRML(vtkMRMLNode* caller, unsigned long event, void *callData /*=nullptr*/)
{
  Superclass::UpdateFromMRML(caller, event, callData);
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::CanInteract(
  vtkMRMLInteractionEventData* interactionEventData,
  int &foundComponentType, int &foundComponentIndex, double &closestDistance2)
{
  foundComponentType = vtkMRMLMarkupsDisplayNode::ComponentNone;
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if ( !markupsNode || markupsNode->GetLocked() || markupsNode->GetNumberOfControlPoints() < 1
    || !this->GetVisibility() || !interactionEventData )
    {
    return;
    }
  Superclass::CanInteract(interactionEventData, foundComponentType, foundComponentIndex, closestDistance2);
  if (foundComponentType != vtkMRMLMarkupsDisplayNode::ComponentNone)
    {
    // if mouse is near a control point then select that (ignore the line)
    return;
    }

}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::GetActors(vtkPropCollection *pc)
{
  //this->PlaneFillActor->GetActors(pc);
  //this->PlaneOutlineActor->GetActors(pc);
  //this->ArrowActor->GetActors(pc);
  this->Superclass::GetActors(pc);
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::ReleaseGraphicsResources(
  vtkWindow *win)
{
  //this->PlaneFillActor->ReleaseGraphicsResources(win);
  //this->PlaneOutlineActor->ReleaseGraphicsResources(win);
  //this->ArrowActor->ReleaseGraphicsResources(win);
  this->Superclass::ReleaseGraphicsResources(win);
}

//----------------------------------------------------------------------
int vtkSlicerROIRepresentation2D::RenderOverlay(vtkViewport *viewport)
{
  int count = Superclass::RenderOverlay(viewport);
  //if (this->PlaneFillActor->GetVisibility())
  //  {
  //  count +=  this->PlaneFillActor->RenderOverlay(viewport);
  //  }
  //if (this->PlaneOutlineActor->GetVisibility())
  //  {
  //  count +=  this->PlaneOutlineActor->RenderOverlay(viewport);
  //  }
  //if (this->ArrowActor->GetVisibility())
  //  {
  //  count +=  this->ArrowActor->RenderOverlay(viewport);
  //  }
  return count;
}

//-----------------------------------------------------------------------------
int vtkSlicerROIRepresentation2D::RenderOpaqueGeometry(
  vtkViewport *viewport)
{
  int count = Superclass::RenderOpaqueGeometry(viewport);
  //if (this->PlaneFillActor->GetVisibility())
  //  {
  //  count += this->PlaneFillActor->RenderOpaqueGeometry(viewport);
  //  }
  //if (this->PlaneOutlineActor->GetVisibility())
  //  {
  //  count += this->PlaneOutlineActor->RenderOpaqueGeometry(viewport);
  //  }
  //if (this->ArrowActor->GetVisibility())
  //  {
  //  count += this->ArrowActor->RenderOpaqueGeometry(viewport);
  //  }

  return count;
}

//-----------------------------------------------------------------------------
int vtkSlicerROIRepresentation2D::RenderTranslucentPolygonalGeometry(
  vtkViewport *viewport)
{
  int count = Superclass::RenderTranslucentPolygonalGeometry(viewport);
  /*if (this->PlaneFillActor->GetVisibility())
    {
    count += this->PlaneFillActor->RenderTranslucentPolygonalGeometry(viewport);
    }
  if (this->PlaneOutlineActor->GetVisibility())
    {
    count += this->PlaneOutlineActor->RenderTranslucentPolygonalGeometry(viewport);
    }
  if (this->ArrowActor->GetVisibility())
    {
    count += this->ArrowActor->RenderTranslucentPolygonalGeometry(viewport);
    }*/

  return count;
}

//-----------------------------------------------------------------------------
vtkTypeBool vtkSlicerROIRepresentation2D::HasTranslucentPolygonalGeometry()
{
  if (this->Superclass::HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  //if (this->PlaneFillActor->GetVisibility() && this->PlaneFillActor->HasTranslucentPolygonalGeometry())
  //  {
  //  return true;
  //  }
  //if (this->PlaneOutlineActor->GetVisibility() && this->PlaneOutlineActor->HasTranslucentPolygonalGeometry())
  //  {
  //  return true;
  //  }
  //if (this->ArrowActor->GetVisibility() && this->ArrowActor->HasTranslucentPolygonalGeometry())
  //  {
  //  return true;
  //  }
  return false;
}

//----------------------------------------------------------------------
double* vtkSlicerROIRepresentation2D::GetBounds()
{
  return nullptr;
}

//-----------------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::PrintSelf(ostream& os, vtkIndent indent)
{
  //Superclass typedef defined in vtkTypeMacro() found in vtkSetGet.h
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::UpdateInteractionPipeline()
{
  Superclass::UpdateInteractionPipeline();
}
