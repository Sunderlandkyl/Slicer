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
#include "vtkMRMLMarkupsROINode.h"
#include "vtkMRMLProceduralColorNode.h"

#include "vtkMRMLMarkupsPlaneNode.h"

#include "vtkMarkupsGlyphSource2D.h"

#include <vtkCutter.h>

vtkStandardNewMacro(vtkSlicerROIRepresentation2D);

#include <vtkCubeSource.h>

//----------------------------------------------------------------------
vtkSlicerROIRepresentation2D::vtkSlicerROIRepresentation2D()
{
  this->CubeWorldToSliceTransformer->SetInputConnection(this->CubeFilter->GetOutputPort());
  this->CubeWorldToSliceTransformer->SetTransform(this->WorldToSliceTransform);
  this->CubeMapper->SetInputConnection(this->CubeWorldToSliceTransformer->GetOutputPort());
  this->CubeProperty->DeepCopy(this->GetControlPointsPipeline(Unselected)->Property);
  this->CubeActor->SetMapper(this->CubeMapper);
  this->CubeActor->SetProperty(this->CubeProperty);

  this->CubeOutlineCutter->SetInputConnection(this->CubeFilter->GetOutputPort());
  this->CubeOutlineCutter->SetCutFunction(this->SlicePlane);
  //this->CubeOutlineCutterCompositeFilter->SetInputConnection(this->CubeOutlineCutter->GetOutputPort());
  this->CubeOutlineWorldToSliceTransformer->SetInputConnection(this->CubeOutlineCutter->GetOutputPort());
  this->CubeOutlineWorldToSliceTransformer->SetTransform(this->WorldToSliceTransform);

  this->CubeOutlineMapper->SetInputConnection(this->CubeOutlineWorldToSliceTransformer->GetOutputPort());
  this->CubeOutlineProperty->DeepCopy(this->GetControlPointsPipeline(Unselected)->Property);
  this->CubeOutlineActor->SetMapper(this->CubeOutlineMapper);
  this->CubeOutlineActor->SetProperty(this->CubeOutlineProperty);
}

//----------------------------------------------------------------------
vtkSlicerROIRepresentation2D::~vtkSlicerROIRepresentation2D() = default;

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::UpdateFromMRML(vtkMRMLNode* caller, unsigned long event, void *callData /*=nullptr*/)
{
  Superclass::UpdateFromMRML(caller, event, callData);

  vtkMRMLMarkupsROINode* roi = vtkMRMLMarkupsROINode::SafeDownCast(this->MarkupsNode);
  if (!roi)
    {
    return;
    }

  int controlPointType = Selected;

  double opacity = this->MarkupsDisplayNode->GetOpacity();

  double fillOpacity = this->MarkupsDisplayNode->GetFillVisibility()
    ? opacity * this->MarkupsDisplayNode->GetFillOpacity() : 0.0;
  this->CubeProperty->DeepCopy(this->GetControlPointsPipeline(controlPointType)->Property);
  this->CubeProperty->SetOpacity(fillOpacity);

  double outlineOpacity = this->MarkupsDisplayNode->GetOutlineVisibility()
    ? opacity * this->MarkupsDisplayNode->GetOutlineOpacity() : 0.0;
  this->CubeOutlineProperty->DeepCopy(this->GetControlPointsPipeline(controlPointType)->Property);
  this->CubeOutlineProperty->SetOpacity(outlineOpacity);


  double* sideLengths = roi->GetSideLengths();
  this->CubeFilter->SetXLength(sideLengths[0]);
  this->CubeFilter->SetYLength(sideLengths[1]);
  this->CubeFilter->SetZLength(sideLengths[2]);
  this->CubeFilter->SetCenter(roi->GetOrigin()); // TODO: GetOriginWorld
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
  this->CubeActor->GetActors(pc);
  this->CubeOutlineActor->GetActors(pc);
  this->Superclass::GetActors(pc);
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::ReleaseGraphicsResources(
  vtkWindow *win)
{
  this->CubeActor->ReleaseGraphicsResources(win);
  this->CubeOutlineActor->ReleaseGraphicsResources(win);
  this->Superclass::ReleaseGraphicsResources(win);
}

//----------------------------------------------------------------------
int vtkSlicerROIRepresentation2D::RenderOverlay(vtkViewport *viewport)
{
  int count = Superclass::RenderOverlay(viewport);
  if (this->CubeActor->GetVisibility())
    {
    count +=  this->CubeActor->RenderOverlay(viewport);
    }
  if (this->CubeOutlineActor->GetVisibility())
    {
    count +=  this->CubeOutlineActor->RenderOverlay(viewport);
    }
  return count;
}

//-----------------------------------------------------------------------------
int vtkSlicerROIRepresentation2D::RenderOpaqueGeometry(
  vtkViewport *viewport)
{
  int count = Superclass::RenderOpaqueGeometry(viewport);
  if (this->CubeActor->GetVisibility())
    {
    count += this->CubeActor->RenderOpaqueGeometry(viewport);
    }
  if (this->CubeOutlineActor->GetVisibility())
    {
    count += this->CubeOutlineActor->RenderOpaqueGeometry(viewport);
    }
  return count;
}

//-----------------------------------------------------------------------------
int vtkSlicerROIRepresentation2D::RenderTranslucentPolygonalGeometry(
  vtkViewport *viewport)
{
  int count = Superclass::RenderTranslucentPolygonalGeometry(viewport);
  if (this->CubeActor->GetVisibility())
    {
    count += this->CubeActor->RenderTranslucentPolygonalGeometry(viewport);
    }
  if (this->CubeOutlineActor->GetVisibility())
    {
    count += this->CubeOutlineActor->RenderTranslucentPolygonalGeometry(viewport);
    }
  return count;
}

//-----------------------------------------------------------------------------
vtkTypeBool vtkSlicerROIRepresentation2D::HasTranslucentPolygonalGeometry()
{
  if (this->Superclass::HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  if (this->CubeActor->GetVisibility() && this->CubeActor->HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  if (this->CubeOutlineActor->GetVisibility() && this->CubeOutlineActor->HasTranslucentPolygonalGeometry())
    {
    return true;
    }
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
