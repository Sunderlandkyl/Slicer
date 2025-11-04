/*=========================================================================

 Copyright (c) ProxSim ltd., Kwun Tong, Hong Kong. All Rights Reserved.

 See COPYRIGHT.txt
 or http://www.slicer.org/copyright/copyright.txt for details.

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.

 This file was originally developed by Davide Punzo, punzodavide@hotmail.it,
 and development was supported by ProxSim ltd.

=========================================================================*/

// VTK includes
#include "vtkCamera.h"
#include "vtkCellLocator.h"
#include "vtkDiscretizableColorTransferFunction.h"
#include "vtkGlyph2D.h"
#include "vtkLabelPlacementMapper.h"
#include "vtkLine.h"
#include "vtkMarkupsGlyphSource2D.h"
#include "vtkMath.h"
#include "vtkMatrix4x4.h"
#include "vtkObjectFactory.h"
#include "vtkPiecewiseFunction.h"
#include "vtkPlane.h"
#include "vtkPointData.h"
#include "vtkPointSetToLabelHierarchy.h"
#include "vtkPolyDataMapper2D.h"
#include "vtkProperty2D.h"
#include "vtkRenderer.h"
#include "vtkRenderWindow.h"
#include "vtkSlicerMarkupsWidgetRepresentation2D.h"
#include "vtkSphereSource.h"
#include "vtkStringArray.h"
#include "vtkTensorGlyph.h"
#include "vtkTextActor.h"
#include "vtkTextProperty.h"
#include "vtkTransform.h"
#include "vtkTransformPolyDataFilter.h"

// MRML includes
#include <vtkMRMLFolderDisplayNode.h>
#include <vtkMRMLInteractionEventData.h>
#include <vtkMRMLScene.h>
#include <vtkMRMLSliceNode.h>

// STD includes
#include <algorithm>

vtkSlicerMarkupsWidgetRepresentation2D::ControlPointsPipeline2D::ControlPointsPipeline2D()
{
  this->Glypher = vtkSmartPointer<vtkGlyph2D>::New();
  this->Glypher->SetInputData(this->ControlPointsPolyData);
  this->Glypher->SetScaleFactor(1.0);

  // By default the Points are rendered as spheres
  this->Glypher->SetSourceConnection(this->GlyphSourceSphere->GetOutputPort());

  this->Property = vtkSmartPointer<vtkProperty2D>::New();
  this->Property->SetColor(0.4, 1.0, 1.0);
  this->Property->SetPointSize(3.);
  this->Property->SetLineWidth(3.);
  this->Property->SetOpacity(1.);

  this->Mapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
  this->Mapper->SetInputConnection(this->Glypher->GetOutputPort());
  this->Mapper->ScalarVisibilityOff();
  vtkNew<vtkCoordinate> coordinate;
  // coordinate->SetViewport(this->Renderer); TODO: check this
  //  World coordinates of the node can not be used to build the actors of the representation
  //  Because the world coordinate in the node are the 3D ones.
  coordinate->SetCoordinateSystemToDisplay();
  this->Mapper->SetTransformCoordinate(coordinate);

  this->Actor = vtkSmartPointer<vtkActor2D>::New();
  this->Actor->SetMapper(this->Mapper);
  this->Actor->SetProperty(this->Property);

  // Labels
  this->LabelsMapper = vtkSmartPointer<vtkLabelPlacementMapper>::New();

  // PointSetToLabelHierarchyFilter perturbs (slightly modifies) label positions around
  // coincident points, but the result is far from optimal (labels are quite far from
  // the control point and not at the same distance).
  // We set maximum depth from the default 5 to 10 to essentially eliminate
  // this coincident label position perturbation (perturbation may still be visible
  // when zooming into the view very much). Probably it would be cleaner to have on option
  // to disable this label position adjustment in PointSetToLabelHierarchyFilter.
  this->PointSetToLabelHierarchyFilter->SetMaximumDepth(10);

  this->LabelsMapper->PlaceAllLabelsOn();

  this->LabelsMapper->SetInputConnection(this->PointSetToLabelHierarchyFilter->GetOutputPort());
  // Here it will be the best to use Display coordinate system
  // in that way we would not need the additional copy of the polydata in LabelFocalData (in Viewport coordinates)
  // However the LabelsMapper seems not working with the Display coordinates.
  this->LabelsMapper->GetAnchorTransform()->SetCoordinateSystemToNormalizedViewport();
  this->LabelsActor = vtkSmartPointer<vtkActor2D>::New();
  this->LabelsActor->SetMapper(this->LabelsMapper);
}

vtkSlicerMarkupsWidgetRepresentation2D::ControlPointsPipeline2D::~ControlPointsPipeline2D() = default;

//----------------------------------------------------------------------
vtkSlicerMarkupsWidgetRepresentation2D::vtkSlicerMarkupsWidgetRepresentation2D()
{
  for (int i = 0; i < NumberOfControlPointTypes; i++)
  {
    this->ControlPoints[i] = new ControlPointsPipeline2D;
  }

  this->ControlPoints[Selected]->TextProperty->SetColor(1.0, 0.5, 0.5);
  reinterpret_cast<ControlPointsPipeline2D*>(this->ControlPoints[Selected])->Property->SetColor(1.0, 0.5, 0.5);

  this->ControlPoints[Active]->TextProperty->SetColor(0.4, 1.0, 0.); // bright green
  reinterpret_cast<ControlPointsPipeline2D*>(this->ControlPoints[Active])->Property->SetColor(0.4, 1.0, 0.);

  this->TextActor->SetTextProperty(this->GetControlPointsPipeline(Unselected)->TextProperty);

  this->PointsVisibilityOnSlice = vtkSmartPointer<vtkIntArray>::New();
  this->PointsVisibilityOnSlice->SetName("pointsVisibilityOnSlice");
  this->PointsVisibilityOnSlice->Allocate(100);
  this->PointsVisibilityOnSlice->SetNumberOfValues(1);
  this->PointsVisibilityOnSlice->SetValue(0, 0);

  this->SlicePlane = vtkSmartPointer<vtkPlane>::New();
  this->WorldToSliceTransform = vtkSmartPointer<vtkTransform>::New();

  this->PropertiesLabelLineMapper->SetInputData(this->PropertiesLabelLinePolyData);
  this->PropertiesLabelLineActor->SetMapper(this->PropertiesLabelLineMapper);
  this->PropertiesLabelLineActor->SetVisibility(false);
}

//----------------------------------------------------------------------
vtkSlicerMarkupsWidgetRepresentation2D::~vtkSlicerMarkupsWidgetRepresentation2D() = default;

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::GetSliceToWorldCoordinates(const double slicePos[2], double worldPos[3])
{
  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (!this->Renderer || !sliceNode)
  {
    return;
  }

  double xyzw[4] = { slicePos[0] - this->Renderer->GetOrigin()[0], slicePos[1] - this->Renderer->GetOrigin()[1], 0.0, 1.0 };
  double rasw[4] = { 0.0, 0.0, 0.0, 1.0 };

  this->GetSliceNode()->GetXYToRAS()->MultiplyPoint(xyzw, rasw);

  worldPos[0] = rasw[0] / rasw[3];
  worldPos[1] = rasw[1] / rasw[3];
  worldPos[2] = rasw[2] / rasw[3];
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::GetWorldToSliceCoordinates(const double worldPos[3], double slicePos[2])
{
  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (!sliceNode)
  {
    return;
  }

  double sliceCoordinates[4], worldCoordinates[4];
  worldCoordinates[0] = worldPos[0];
  worldCoordinates[1] = worldPos[1];
  worldCoordinates[2] = worldPos[2];
  worldCoordinates[3] = 1.0;

  vtkNew<vtkMatrix4x4> rasToxyMatrix;
  sliceNode->GetXYToRAS()->Invert(sliceNode->GetXYToRAS(), rasToxyMatrix.GetPointer());

  rasToxyMatrix->MultiplyPoint(worldCoordinates, sliceCoordinates);

  slicePos[0] = sliceCoordinates[0];
  slicePos[1] = sliceCoordinates[1];
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::UpdateAllPointsAndLabelsFromMRML(double labelsOffset)
{
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!this->ViewNode || !markupsNode || !this->MarkupsDisplayNode || !this->Renderer)
  {
    return;
  }

  // Use first active control point for jumping //TODO: Have an 'even more active' point concept
  std::vector<int> activeControlPointIndices;
  this->MarkupsDisplayNode->GetActiveControlPoints(activeControlPointIndices);
  int activeControlPointIndex = -1;
  if (!activeControlPointIndices.empty())
  {
    activeControlPointIndex = activeControlPointIndices[0];
  }

  int numPoints = markupsNode->GetNumberOfControlPoints();

  for (int controlPointType = 0; controlPointType < NumberOfControlPointTypes; ++controlPointType)
  {
    ControlPointsPipeline2D* controlPoints = reinterpret_cast<ControlPointsPipeline2D*>(this->ControlPoints[controlPointType]);

    controlPoints->ControlPoints->Reset();
    controlPoints->ControlPointsPolyData->GetPointData()->GetNormals()->Reset();

    controlPoints->LabelControlPoints->Reset();
    controlPoints->LabelControlPointsPolyData->GetPointData()->GetNormals()->Reset();
    controlPoints->Labels->Reset();
    controlPoints->LabelsPriority->Reset();

    int startIndex = 0;
    int stopIndex = numPoints - 1;
    if (controlPointType == Active)
    {
      if (activeControlPointIndex >= 0 && activeControlPointIndex < numPoints &&        //
          markupsNode->GetNthControlPointPositionVisibility(activeControlPointIndex) && //
          markupsNode->GetNthControlPointPositionVisibility(activeControlPointIndex) && //
          ((this->PointsVisibilityOnSlice->GetValue(activeControlPointIndex) &&         //
            !this->MarkupsDisplayNode->GetSliceProjection())
           || //
           this->MarkupsDisplayNode->GetSliceProjection()))
      {
        startIndex = activeControlPointIndex;
        stopIndex = startIndex;
      }
      else
      {
        controlPoints->Actor->VisibilityOff();
        controlPoints->LabelsActor->VisibilityOff();
        continue;
      }
    }

    if (controlPointType >= Project && //
        !this->MarkupsDisplayNode->GetSliceProjection())
    {
      continue;
    }

    if (controlPointType == ProjectBack && //
        !this->MarkupsDisplayNode->GetSliceProjectionOutlinedBehindSlicePlane())
    {
      continue;
    }

    for (int pointIndex = startIndex; pointIndex <= stopIndex; pointIndex++)
    {
      if (!(markupsNode->GetNthControlPointPositionVisibility(pointIndex) && markupsNode->GetNthControlPointVisibility(pointIndex)) || //
          (controlPointType < Active &&                                                                                                //
           !this->PointsVisibilityOnSlice->GetValue(pointIndex))
          ||                            //
          (controlPointType > Active && //
           this->PointsVisibilityOnSlice->GetValue(pointIndex)))
      {
        continue;
      }
      if (controlPointType == Project &&                                            //
          this->MarkupsDisplayNode->GetSliceProjectionOutlinedBehindSlicePlane() && //
          !this->IsPointInFrontSlice(markupsNode, pointIndex))
      {
        continue;
      }
      if (controlPointType == ProjectBack &&                                        //
          this->MarkupsDisplayNode->GetSliceProjectionOutlinedBehindSlicePlane() && //
          !this->IsPointBehindSlice(markupsNode, pointIndex))
      {
        continue;
      }
      if (controlPointType < Active)
      {
        bool thisNodeSelected = (markupsNode->GetNthControlPointSelected(pointIndex) != 0);
        if ((controlPointType == Selected) != thisNodeSelected)
        {
          continue;
        }
        if (pointIndex == activeControlPointIndex)
        {
          continue;
        }
      }
      if (controlPointType > Active && pointIndex == activeControlPointIndex)
      {
        continue;
      }

      double slicePos[3] = { 0.0 };
      this->GetNthControlPointDisplayPosition(pointIndex, slicePos);

      controlPoints->ControlPoints->InsertNextPoint(slicePos);
      slicePos[0] += labelsOffset / sqrt(2.0);
      slicePos[1] += labelsOffset / sqrt(2.0);
      this->Renderer->SetDisplayPoint(slicePos);
      this->Renderer->DisplayToView();
      double viewPos[3] = { 0.0 };
      this->Renderer->GetViewPoint(viewPos);
      this->Renderer->ViewToNormalizedViewport(viewPos[0], viewPos[1], viewPos[2]);
      controlPoints->LabelControlPoints->InsertNextPoint(viewPos);

      double pointNormalWorld[3] = { 0.0, 0.0, 1.0 };
      markupsNode->GetNthControlPointNormalWorld(pointIndex, pointNormalWorld);
      // probably we should transform this orientation to display coordinate system
      controlPoints->ControlPointsPolyData->GetPointData()->GetNormals()->InsertNextTuple(pointNormalWorld);
      controlPoints->LabelControlPointsPolyData->GetPointData()->GetNormals()->InsertNextTuple(pointNormalWorld);

      controlPoints->Labels->InsertNextValue(markupsNode->GetNthControlPointLabel(pointIndex));
      controlPoints->LabelsPriority->InsertNextValue(std::to_string(pointIndex));
    }

    controlPoints->ControlPoints->Modified();
    controlPoints->ControlPointsPolyData->GetPointData()->GetNormals()->Modified();
    controlPoints->ControlPointsPolyData->Modified();

    controlPoints->LabelControlPoints->Modified();
    controlPoints->LabelControlPointsPolyData->GetPointData()->GetNormals()->Modified();
    controlPoints->LabelControlPointsPolyData->Modified();

    if (controlPointType == Active)
    {
      controlPoints->Actor->VisibilityOn();
      // For backward compatibility, we hide labels if text scale is set to 0.
      controlPoints->LabelsActor->SetVisibility(this->MarkupsDisplayNode->GetPointLabelsVisibility() //
                                                && this->MarkupsDisplayNode->GetTextScale() > 0.0);
    }
  }
}

//----------------------------------------------------------------------
double vtkSlicerMarkupsWidgetRepresentation2D::GetWidgetOpacity(int controlPointType)
{
  if (!this->MarkupsDisplayNode)
  {
    return 1.0;
  }

  // Use hierarchy information if any, and if overriding is allowed for the current display node
  double hierarchyOpacity = 1.0;
  if (this->MarkupsDisplayNode->GetFolderDisplayOverrideAllowed())
  {
    vtkMRMLDisplayableNode* displayableNode = this->MarkupsDisplayNode->GetDisplayableNode();
    hierarchyOpacity = vtkMRMLFolderDisplayNode::GetHierarchyOpacity(displayableNode);
  }

  switch (controlPointType)
  {
    case Unselected: return this->MarkupsDisplayNode->GetOpacity() * hierarchyOpacity;
    case Selected: return this->MarkupsDisplayNode->GetOpacity() * hierarchyOpacity;
    case Active: return this->MarkupsDisplayNode->GetOpacity() * hierarchyOpacity;
    case Project: return this->MarkupsDisplayNode->GetSliceProjectionOpacity() * hierarchyOpacity;
    case ProjectBack: return this->MarkupsDisplayNode->GetSliceProjectionOpacity() * hierarchyOpacity;
    default: return 1.0;
  }
}

//----------------------------------------------------------------------
vtkMRMLSliceNode* vtkSlicerMarkupsWidgetRepresentation2D::GetSliceNode()
{
  return vtkMRMLSliceNode::SafeDownCast(this->ViewNode);
}

//----------------------------------------------------------------------
int vtkSlicerMarkupsWidgetRepresentation2D::GetNthControlPointDisplayPosition(int n, double slicePos[2])
{
  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode || n < 0 || n >= markupsNode->GetNumberOfControlPoints() || !sliceNode)
  {
    return 0;
  }
  double worldPos[3];
  markupsNode->GetNthControlPointPositionWorld(n, worldPos);
  this->GetWorldToSliceCoordinates(worldPos, slicePos);
  return 1;
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::SetNthControlPointSliceVisibility(int n, bool visibility)
{
  this->PointsVisibilityOnSlice->InsertValue(n, visibility);
  this->Modified();
}

//----------------------------------------------------------------------
bool vtkSlicerMarkupsWidgetRepresentation2D::GetNthControlPointViewVisibility(int n)
{
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode || !this->GetVisibility())
  {
    return false;
  }
  if (!(markupsNode->GetNthControlPointPositionVisibility(n) //
        && (markupsNode->GetNthControlPointVisibility(n))))
  {
    return false;
  }
  return (this->PointsVisibilityOnSlice->GetValue(n) != 0 || this->MarkupsDisplayNode->GetSliceProjection());
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::SetCenterSliceVisibility(bool visibility)
{
  this->CenterVisibilityOnSlice = visibility;
  this->Modified();
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::UpdateFromMRMLInternal(vtkMRMLNode* caller, unsigned long event, void* callData /*=nullptr*/)
{
  Superclass::UpdateFromMRMLInternal(caller, event, callData);

  // Update from slice node
  if (!caller || caller == this->ViewNode.GetPointer())
  {
    this->UpdateViewScaleFactor();
    this->UpdatePlaneFromSliceNode();
  }

  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode || !this->IsDisplayable())
  {
    this->VisibilityOff();
    return;
  }

  for (int controlPointType = 0; controlPointType < NumberOfControlPointTypes; ++controlPointType)
  {
    double* color = this->GetWidgetColor(controlPointType);
    double opacity = this->GetWidgetOpacity(controlPointType);

    ControlPointsPipeline2D* controlPoints = this->GetControlPointsPipeline(controlPointType);
    controlPoints->Property->SetColor(color);
    controlPoints->Property->SetOpacity(opacity);

    controlPoints->TextProperty->ShallowCopy(this->MarkupsDisplayNode->GetTextProperty());
    if (this->GetApplicationLogic())
    {
      this->GetApplicationLogic()->UseCustomFontFile(controlPoints->TextProperty);
    }
    controlPoints->TextProperty->SetColor(color);
    controlPoints->TextProperty->SetOpacity(opacity);
    controlPoints->TextProperty->SetFontSize(
      static_cast<int>(this->MarkupsDisplayNode->GetTextProperty()->GetFontSize() * this->MarkupsDisplayNode->GetTextScale() * this->GetScreenScaleFactor() * 5.0));
    controlPoints->TextProperty->SetBackgroundOpacity(opacity * this->MarkupsDisplayNode->GetTextProperty()->GetBackgroundOpacity());

    vtkMarkupsGlyphSource2D* glyphSource = this->GetControlPointsPipeline(controlPointType)->GlyphSource2D;
    if (this->MarkupsDisplayNode->GlyphTypeIs3D())
    {
      // Sphere is the only 3D glyph, use 2D circle in default
      glyphSource->SetGlyphTypeToCircle();
    }
    else
    {
      glyphSource->SetGlyphType(this->GetGlyphTypeSourceFromDisplay(this->MarkupsDisplayNode->GetGlyphType()));
    }

    if (this->MarkupsDisplayNode->GetSliceProjectionOutlinedBehindSlicePlane() && controlPointType == ProjectBack)
    {
      glyphSource->FilledOff();
    }
    else
    {
      glyphSource->FilledOn();
    }
    this->GetControlPointsPipeline(controlPointType)->Glypher->SetSourceConnection(glyphSource->GetOutputPort());
  }

  this->UpdateControlPointSize();

  // Points widgets have only one Markup/Representation
  this->AnyPointVisibilityOnSlice = false;
  for (int pointIndex = 0; pointIndex < markupsNode->GetNumberOfControlPoints(); pointIndex++)
  {
    bool visibility = this->IsControlPointDisplayableOnSlice(markupsNode, pointIndex);
    if (visibility)
    {
      this->AnyPointVisibilityOnSlice = true;
    }
    this->SetNthControlPointSliceVisibility(pointIndex, visibility);
  }

  if (markupsNode->GetCurveClosed())
  {
    bool visibility = this->IsCenterDisplayableOnSlice(markupsNode);
    this->SetCenterSliceVisibility(visibility);
  }

  for (int controlPointType = 0; controlPointType < NumberOfControlPointTypes; ++controlPointType)
  {
    ControlPointsPipeline2D* controlPoints = reinterpret_cast<ControlPointsPipeline2D*>(this->ControlPoints[controlPointType]);
    // For backward compatibility, we hide labels if text scale is set to 0.
    controlPoints->LabelsActor->SetVisibility(this->MarkupsDisplayNode->GetPointLabelsVisibility() //
                                              && this->MarkupsDisplayNode->GetTextScale() > 0.0);
    controlPoints->Glypher->SetScaleFactor(this->ControlPointSize);
  }

  // put the labels near the boundary of the glyph, slightly away from it (by half picking tolarance)
  double labelsOffset = this->ControlPointSize * 0.5 + this->PickingTolerance * 0.5 * this->GetScreenScaleFactor();
  this->UpdateAllPointsAndLabelsFromMRML(labelsOffset);

  // Update label position if custom positioning is enabled
  this->UpdatePropertiesLabelPosition();

  this->VisibilityOn();
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::CanInteract(vtkMRMLInteractionEventData* interactionEventData,
                                                         int& foundComponentType,
                                                         int& foundComponentIndex,
                                                         double& closestDistance2)
{
  foundComponentType = vtkMRMLMarkupsDisplayNode::ComponentNone;
  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!sliceNode || !markupsNode || markupsNode->GetLocked() || !this->GetVisibility() || !interactionEventData)
  {
    return;
  }

  const int* displayPosition = interactionEventData->GetDisplayPosition();
  double displayPosition3[3] = { static_cast<double>(displayPosition[0]), static_cast<double>(displayPosition[1]), 0.0 };

  this->UpdateControlPointSize();
  double maxPickingDistanceFromControlPoint2 = this->GetMaximumControlPointPickingDistance2();

  closestDistance2 = VTK_DOUBLE_MAX; // in display coordinate system
  foundComponentIndex = -1;

  if (markupsNode->GetNumberOfControlPoints() > 2 && this->CurveClosed && this->CenterVisibilityOnSlice)
  {
    // Check if center is selected
    double centerPosWorld[3], centerPosDisplay[3];
    markupsNode->GetCenterOfRotationWorld(centerPosWorld);
    this->GetWorldToSliceCoordinates(centerPosWorld, centerPosDisplay);

    double dist2 = vtkMath::Distance2BetweenPoints(centerPosDisplay, displayPosition3);
    if (dist2 < maxPickingDistanceFromControlPoint2)
    {
      closestDistance2 = dist2;
      foundComponentType = vtkMRMLMarkupsDisplayNode::ComponentCenterPoint;
      foundComponentIndex = 0;
    }
  }

  vtkIdType numberOfPoints = markupsNode->GetNumberOfControlPoints();

  double pointDisplayPos[4] = { 0.0, 0.0, 0.0, 1.0 };
  double pointWorldPos[4] = { 0.0, 0.0, 0.0, 1.0 };

  vtkNew<vtkMatrix4x4> rasToxyMatrix;
  sliceNode->GetXYToRAS()->Invert(sliceNode->GetXYToRAS(), rasToxyMatrix.GetPointer());
  for (int i = 0; i < numberOfPoints; i++)
  {
    if (!this->GetNthControlPointViewVisibility(i))
    {
      continue;
    }
    markupsNode->GetNthControlPointPositionWorld(i, pointWorldPos);
    rasToxyMatrix->MultiplyPoint(pointWorldPos, pointDisplayPos);
    if (this->MarkupsDisplayNode->GetSliceProjection())
    {
      pointDisplayPos[2] = displayPosition3[2];
    }
    double dist2 = vtkMath::Distance2BetweenPoints(pointDisplayPos, displayPosition3);
    if (dist2 < maxPickingDistanceFromControlPoint2 && dist2 < closestDistance2)
    {
      closestDistance2 = dist2;
      foundComponentType = vtkMRMLMarkupsDisplayNode::ComponentControlPoint;
      foundComponentIndex = i;
    }
  }
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::CanInteractWithLine(vtkMRMLInteractionEventData* interactionEventData,
                                                                 int& foundComponentType,
                                                                 int& foundComponentIndex,
                                                                 double& closestDistance2)
{
  foundComponentType = vtkMRMLMarkupsDisplayNode::ComponentNone;

  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!sliceNode || !markupsNode || markupsNode->GetLocked() || markupsNode->GetNumberOfControlPoints() < 1 //
      || !this->GetVisibility() || !interactionEventData)
  {
    return;
  }

  const int* displayPosition = interactionEventData->GetDisplayPosition();
  double displayPosition3[3] = { static_cast<double>(displayPosition[0]), static_cast<double>(displayPosition[1]), 0.0 };
  double maxPickingDistanceFromControlPoint2 = this->GetMaximumControlPointPickingDistance2();

  vtkIdType numberOfPoints = markupsNode->GetNumberOfControlPoints();

  double pointDisplayPos1[4] = { 0.0, 0.0, 0.0, 1.0 };
  double pointWorldPos1[4] = { 0.0, 0.0, 0.0, 1.0 };
  double pointDisplayPos2[4] = { 0.0, 0.0, 0.0, 1.0 };
  double pointWorldPos2[4] = { 0.0, 0.0, 0.0, 1.0 };

  vtkNew<vtkMatrix4x4> rasToxyMatrix;
  sliceNode->GetXYToRAS()->Invert(sliceNode->GetXYToRAS(), rasToxyMatrix.GetPointer());
  for (int i = 0; i < numberOfPoints - 1; i++)
  {
    if (!this->PointsVisibilityOnSlice->GetValue(i))
    {
      continue;
    }
    if (!this->PointsVisibilityOnSlice->GetValue(i + 1))
    {
      i++; // skip one more, as the next iteration would use (i+1)-th point
      continue;
    }
    markupsNode->GetNthControlPointPositionWorld(i, pointWorldPos1);
    rasToxyMatrix->MultiplyPoint(pointWorldPos1, pointDisplayPos1);
    markupsNode->GetNthControlPointPositionWorld(i + 1, pointWorldPos2);
    rasToxyMatrix->MultiplyPoint(pointWorldPos2, pointDisplayPos2);

    double relativePositionAlongLine = -1.0; // between 0.0-1.0 if between the endpoints of the line segment
    double distance2 = vtkLine::DistanceToLine(displayPosition3, pointDisplayPos1, pointDisplayPos2, relativePositionAlongLine);
    if (distance2 < maxPickingDistanceFromControlPoint2 && distance2 < closestDistance2 && relativePositionAlongLine >= 0 && relativePositionAlongLine <= 1)
    {
      closestDistance2 = distance2;
      foundComponentType = vtkMRMLMarkupsDisplayNode::ComponentLine;
      foundComponentIndex = i;
    }
  }
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::GetActors(vtkPropCollection* pc)
{
  Superclass::GetActors(pc);
  for (int i = 0; i < NumberOfControlPointTypes; i++)
  {
    ControlPointsPipeline2D* controlPoints = reinterpret_cast<ControlPointsPipeline2D*>(this->ControlPoints[i]);
    controlPoints->Actor->GetActors(pc);
    controlPoints->LabelsActor->GetActors(pc);
  }
  this->TextActor->GetActors(pc);
  this->PropertiesLabelLineActor->GetActors(pc);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::ReleaseGraphicsResources(vtkWindow* win)
{
  Superclass::ReleaseGraphicsResources(win);
  for (int i = 0; i < NumberOfControlPointTypes; i++)
  {
    ControlPointsPipeline2D* controlPoints = reinterpret_cast<ControlPointsPipeline2D*>(this->ControlPoints[i]);
    controlPoints->Actor->ReleaseGraphicsResources(win);
    controlPoints->LabelsActor->ReleaseGraphicsResources(win);
  }
  this->TextActor->ReleaseGraphicsResources(win);
}

//----------------------------------------------------------------------
int vtkSlicerMarkupsWidgetRepresentation2D::RenderOverlay(vtkViewport* viewport)
{
  int count = Superclass::RenderOverlay(viewport);
  for (int i = 0; i < NumberOfControlPointTypes; i++)
  {
    ControlPointsPipeline2D* controlPoints = reinterpret_cast<ControlPointsPipeline2D*>(this->ControlPoints[i]);
    if (controlPoints->Actor->GetVisibility())
    {
      count += controlPoints->Actor->RenderOverlay(viewport);
    }
    if (controlPoints->LabelsActor->GetVisibility())
    {
      count += controlPoints->LabelsActor->RenderOverlay(viewport);
    }
  }

  // Update label position if custom positioning is enabled
  bool shouldRenderLabel = this->UpdatePropertiesLabelPosition();

  if (this->TextActor->GetVisibility() && shouldRenderLabel)
  {
    count += this->TextActor->RenderOverlay(viewport);
  }

  count += this->PropertiesLabelLineActor->RenderOverlay(viewport);
  return count;
}

//-----------------------------------------------------------------------------
int vtkSlicerMarkupsWidgetRepresentation2D::RenderOpaqueGeometry(vtkViewport* viewport)
{
  int count = 0;
  if (this->TextActor->GetVisibility())
  {
    count += this->TextActor->RenderOpaqueGeometry(viewport);
  }
  for (int i = 0; i < NumberOfControlPointTypes; i++)
  {
    ControlPointsPipeline2D* controlPoints = reinterpret_cast<ControlPointsPipeline2D*>(this->ControlPoints[i]);
    if (controlPoints->Actor->GetVisibility())
    {
      count += controlPoints->Actor->RenderOpaqueGeometry(viewport);
    }
    if (controlPoints->LabelsActor->GetVisibility())
    {
      count += controlPoints->LabelsActor->RenderOpaqueGeometry(viewport);
    }
  }
  return count;
}

//-----------------------------------------------------------------------------
int vtkSlicerMarkupsWidgetRepresentation2D::RenderTranslucentPolygonalGeometry(vtkViewport* viewport)
{
  int count = Superclass::RenderTranslucentPolygonalGeometry(viewport);
  if (this->TextActor->GetVisibility())
  {
    count += this->TextActor->RenderTranslucentPolygonalGeometry(viewport);
  }
  for (int i = 0; i < NumberOfControlPointTypes; i++)
  {
    ControlPointsPipeline2D* controlPoints = reinterpret_cast<ControlPointsPipeline2D*>(this->ControlPoints[i]);
    if (controlPoints->Actor->GetVisibility())
    {
      count += controlPoints->Actor->RenderTranslucentPolygonalGeometry(viewport);
    }
    if (controlPoints->LabelsActor->GetVisibility())
    {
      count += controlPoints->LabelsActor->RenderTranslucentPolygonalGeometry(viewport);
    }
  }
  return count;
}

//-----------------------------------------------------------------------------
vtkTypeBool vtkSlicerMarkupsWidgetRepresentation2D::HasTranslucentPolygonalGeometry()
{
  if (this->Superclass::HasTranslucentPolygonalGeometry())
  {
    return true;
  }
  if (this->TextActor->GetVisibility() && this->TextActor->HasTranslucentPolygonalGeometry())
  {
    return true;
  }
  for (int i = 0; i < NumberOfControlPointTypes; i++)
  {
    ControlPointsPipeline2D* controlPoints = reinterpret_cast<ControlPointsPipeline2D*>(this->ControlPoints[i]);
    if (controlPoints->Actor->GetVisibility() && controlPoints->Actor->HasTranslucentPolygonalGeometry())
    {
      return true;
    }
    if (controlPoints->LabelsActor->GetVisibility() && controlPoints->LabelsActor->HasTranslucentPolygonalGeometry())
    {
      return true;
    }
  }
  return false;
}

//-----------------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::PrintSelf(ostream& os, vtkIndent indent)
{
  // Superclass typedef defined in vtkTypeMacro() found in vtkSetGet.h
  this->Superclass::PrintSelf(os, indent);

  if (this->TextActor)
  {
    os << indent << "Text Visibility: " << this->TextActor->GetVisibility() << "\n";
  }
  else
  {
    os << indent << "Text Visibility: (none)\n";
  }

  for (int i = 0; i < NumberOfControlPointTypes; i++)
  {
    ControlPointsPipeline2D* controlPoints = reinterpret_cast<ControlPointsPipeline2D*>(this->ControlPoints[i]);
    os << indent << "Pipeline " << i << "\n";
    if (controlPoints->Actor)
    {
      os << indent << "Points Visibility: " << controlPoints->Actor->GetVisibility() << "\n";
    }
    else
    {
      os << indent << "Points: (none)\n";
    }
    if (controlPoints->LabelsActor)
    {
      os << indent << "Labels Visibility: " << controlPoints->LabelsActor->GetVisibility() << "\n";
    }
    else
    {
      os << indent << "Labels Points: (none)\n";
    }
    if (controlPoints->Property)
    {
      os << indent << "Property: " << controlPoints->Property << "\n";
    }
    else
    {
      os << indent << "Property: (none)\n";
    }
  }
}

//-----------------------------------------------------------------------------
vtkSlicerMarkupsWidgetRepresentation2D::ControlPointsPipeline2D* vtkSlicerMarkupsWidgetRepresentation2D::GetControlPointsPipeline(int controlPointType)
{
  return reinterpret_cast<ControlPointsPipeline2D*>(this->ControlPoints[controlPointType]);
}

//---------------------------------------------------------------------------
bool vtkSlicerMarkupsWidgetRepresentation2D::IsControlPointDisplayableOnSlice(vtkMRMLMarkupsNode* markupsNode, int pointIndex)
{
  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (!sliceNode)
  {
    return false;
  }

  // if there's no node, it's not visible
  if (!markupsNode)
  {
    vtkErrorMacro("IsWidgetDisplayableOnSlice: Could not get the markups node.");
    return false;
  }

  bool showPoint = true;

  // allow nodes to appear only in designated viewers
  vtkMRMLDisplayNode* displayNode = markupsNode->GetDisplayNode();
  if (!displayNode)
  {
    return false;
  }

  double transformedWorldCoordinates[4];
  markupsNode->GetNthControlPointPositionWorld(pointIndex, transformedWorldCoordinates);

  // now get the displayCoordinates for the transformed worldCoordinates
  double displayCoordinates[4];
  this->GetWorldToDisplayCoordinates(transformedWorldCoordinates, displayCoordinates);

  // check if the point is close enough to the slice to be shown
  if (showPoint)
  {
    // the third coordinate of the displayCoordinates is the distance to the slice
    double distanceToSlice = displayCoordinates[2];
    double maxDistance = 0.5 + (sliceNode->GetDimensions()[2] - 1);
    /*vtkDebugMacro("Slice node " << sliceNode->GetName()
      << ": distance to slice = " << distanceToSlice
      << ", maxDistance = " << maxDistance
      << "\n\tslice node dimensions[2] = "
      << sliceNode->GetDimensions()[2]);*/
    if (distanceToSlice < -0.5 || distanceToSlice >= maxDistance)
    {
      // if the distance to the slice is more than 0.5mm, we know that at least one coordinate of the widget is outside the current activeSlice
      // hence, we do not want to show this widget
      showPoint = false;
    }
  }

  return showPoint;
}

//---------------------------------------------------------------------------
bool vtkSlicerMarkupsWidgetRepresentation2D::IsPointBehindSlice(vtkMRMLMarkupsNode* markupsNode, int pointIndex)
{
  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (!sliceNode)
  {
    return false;
  }

  // if there's no node, it's not visible
  if (!markupsNode)
  {
    vtkErrorMacro("IsWidgetDisplayableOnSlice: Could not get the markups node.");
    return false;
  }

  // allow nodes to appear only in designated viewers
  vtkMRMLDisplayNode* displayNode = markupsNode->GetDisplayNode();
  if (!displayNode)
  {
    return false;
  }

  double transformedWorldCoordinates[4];
  markupsNode->GetNthControlPointPositionWorld(pointIndex, transformedWorldCoordinates);

  // now get the displayCoordinates for the transformed worldCoordinates
  double displayCoordinates[4];
  this->GetWorldToDisplayCoordinates(transformedWorldCoordinates, displayCoordinates);

  // the third coordinate of the displayCoordinates is the distance to the slice
  double distanceToSlice = displayCoordinates[2];
  bool pointBehind = distanceToSlice < -0.5;
  return pointBehind;
}

//---------------------------------------------------------------------------
bool vtkSlicerMarkupsWidgetRepresentation2D::IsPointInFrontSlice(vtkMRMLMarkupsNode* markupsNode, int pointIndex)
{
  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (!sliceNode)
  {
    return false;
  }

  // if there's no node, it's not visible
  if (!markupsNode)
  {
    vtkErrorMacro("IsWidgetDisplayableOnSlice: Could not get the markups node.");
    return false;
  }

  // allow nodes to appear only in designated viewers
  vtkMRMLDisplayNode* displayNode = markupsNode->GetDisplayNode();
  if (!displayNode)
  {
    return false;
  }

  double transformedWorldCoordinates[4];
  markupsNode->GetNthControlPointPositionWorld(pointIndex, transformedWorldCoordinates);

  // now get the displayCoordinates for the transformed worldCoordinates
  double displayCoordinates[4];
  this->GetWorldToDisplayCoordinates(transformedWorldCoordinates, displayCoordinates);

  // the third coordinate of the displayCoordinates is the distance to the slice
  double distanceToSlice = displayCoordinates[2];
  bool pointFront = distanceToSlice > 0.5;
  return pointFront;
}

//---------------------------------------------------------------------------
bool vtkSlicerMarkupsWidgetRepresentation2D::IsCenterDisplayableOnSlice(vtkMRMLMarkupsNode* markupsNode)
{
  // if no slice node, it doesn't constrain the visibility, so return that
  // it's visible
  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (!sliceNode)
  {
    vtkErrorMacro("IsWidgetDisplayableOnSlice: Could not get the sliceNode.");
    return false;
  }

  // if there's no node, it's not visible
  if (!markupsNode)
  {
    vtkErrorMacro("IsWidgetDisplayableOnSlice: Could not get the markups node.");
    return false;
  }

  bool showPoint = true;

  // allow nodes to appear only in designated viewers
  if (!markupsNode || !this->IsDisplayable())
  {
    return false;
  }

  // down cast the node as a controlpoints node to get the coordinates
  double transformedWorldCoordinates[4];
  markupsNode->GetCenterOfRotationWorld(transformedWorldCoordinates);

  // now get the displayCoordinates for the transformed worldCoordinates
  double displayCoordinates[4];
  this->GetWorldToDisplayCoordinates(transformedWorldCoordinates, displayCoordinates);

  // check if the point is close enough to the slice to be shown
  if (showPoint)
  {
    // the third coordinate of the displayCoordinates is the distance to the slice
    double distanceToSlice = displayCoordinates[2];
    double maxDistance = 0.5 + (sliceNode->GetDimensions()[2] - 1);
    /*vtkDebugMacro("Slice node " << sliceNode->GetName()
      << ": distance to slice = " << distanceToSlice
      << ", maxDistance = " << maxDistance
      << "\n\tslice node dimensions[2] = "
      << sliceNode->GetDimensions()[2]);*/
    if (distanceToSlice < -0.5 || distanceToSlice >= maxDistance)
    {
      // if the distance to the slice is more than 0.5mm, we know that at least one coordinate of the widget is outside the current activeSlice
      // hence, we do not want to show this widget
      showPoint = false;
    }
  }

  return showPoint;
}

//---------------------------------------------------------------------------
/// Convert world to display coordinates
void vtkSlicerMarkupsWidgetRepresentation2D::GetWorldToDisplayCoordinates(double r, double a, double s, double* displayCoordinates)
{
  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (!sliceNode)
  {
    vtkErrorMacro("GetWorldToDisplayCoordinates: no slice node!");
    return;
  }

  // we will get the transformation matrix to convert world coordinates to the display coordinates of the specific sliceNode

  vtkMatrix4x4* xyToRasMatrix = sliceNode->GetXYToRAS();
  vtkNew<vtkMatrix4x4> rasToXyMatrix;

  // we need to invert this matrix
  xyToRasMatrix->Invert(xyToRasMatrix, rasToXyMatrix.GetPointer());

  double worldCoordinates[4];
  worldCoordinates[0] = r;
  worldCoordinates[1] = a;
  worldCoordinates[2] = s;
  worldCoordinates[3] = 1;

  rasToXyMatrix->MultiplyPoint(worldCoordinates, displayCoordinates);
  xyToRasMatrix = nullptr;
}

//---------------------------------------------------------------------------
// Convert world to display coordinates
void vtkSlicerMarkupsWidgetRepresentation2D::GetWorldToDisplayCoordinates(double* worldCoordinates, double* displayCoordinates)
{
  if (worldCoordinates == nullptr)
  {
    return;
  }

  this->GetWorldToDisplayCoordinates(worldCoordinates[0], worldCoordinates[1], worldCoordinates[2], displayCoordinates);
}

//----------------------------------------------------------------------
bool vtkSlicerMarkupsWidgetRepresentation2D::GetAllControlPointsVisible()
{
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode)
  {
    return false;
  }

  for (int controlPointIndex = 0; controlPointIndex < markupsNode->GetNumberOfControlPoints(); controlPointIndex++)
  {
    if (!this->PointsVisibilityOnSlice->GetValue(controlPointIndex) ||         //
        !(markupsNode->GetNthControlPointPositionVisibility(controlPointIndex) //
          && (markupsNode->GetNthControlPointVisibility(controlPointIndex))))
    {
      return false;
    }
  }
  return true;
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::UpdateDistanceColorMap(vtkDiscretizableColorTransferFunction* colormap, double color[3])
{
  if (colormap == nullptr || this->MarkupsDisplayNode == nullptr)
  {
    return;
  }

  double limit = this->MarkupsDisplayNode->GetLineColorFadingEnd();
  double tolerance = this->MarkupsDisplayNode->GetLineColorFadingStart();
  vtkPiecewiseFunction* opacityFunction = colormap->GetScalarOpacityFunction();
  if (!opacityFunction)
  {
    opacityFunction = vtkPiecewiseFunction::New();
    colormap->SetScalarOpacityFunction(opacityFunction);
    opacityFunction->Delete();
  }
  double opacity = this->MarkupsDisplayNode->GetOpacity();
  opacityFunction->RemoveAllPoints();
  opacityFunction->AddPoint(-limit, 0.);
  opacityFunction->AddPoint(-tolerance, opacity);
  opacityFunction->AddPoint(tolerance, opacity);
  opacityFunction->AddPoint(limit, 0.);

  colormap->RemoveAllPoints();
  double color1[3] = { std::min(color[0] * 0.5, 1.0), std::min(color[1] * 0.5, 1.0), std::min(color[2] * 2.0, 1.0) };
  double color3[3] = { std::min(color[0] * 2.0, 1.0), std::min(color[1] * 0.5, 1.0), std::min(color[2] * 0.5, 1.0) };

  double colorHsv1[3];
  double colorHsv2[3];
  double colorHsv3[3];
  vtkMath::RGBToHSV(color1, colorHsv1);
  vtkMath::RGBToHSV(color, colorHsv2);
  vtkMath::RGBToHSV(color3, colorHsv3);

  double hueOffset = this->MarkupsDisplayNode->GetLineColorFadingHueOffset();
  double saturation = this->MarkupsDisplayNode->GetLineColorFadingSaturation();

  colormap->AddHSVPoint(-tolerance * 2.0, colorHsv1[0] - hueOffset, saturation * colorHsv1[1], colorHsv1[2]);
  colormap->AddHSVPoint(-tolerance, colorHsv2[0] - hueOffset, saturation * colorHsv2[1], colorHsv2[2]);
  colormap->AddHSVPoint(tolerance, colorHsv2[0] - hueOffset, saturation * colorHsv2[1], colorHsv2[2]);
  colormap->AddHSVPoint(tolerance * 2.0, colorHsv3[0] - hueOffset, saturation * colorHsv3[1], colorHsv3[2]);
  colormap->SetEnableOpacityMapping(true);
  colormap->SetClamping(true);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::UpdatePlaneFromSliceNode()
{
  vtkMatrix4x4* sliceXYToRAS = this->GetSliceNode()->GetXYToRAS();

  // Update transformation to slice
  vtkNew<vtkMatrix4x4> rasToSliceXY;
  vtkMatrix4x4::Invert(sliceXYToRAS, rasToSliceXY.GetPointer());
  // Project all points to the slice plane (slice Z coordinate = 0)
  rasToSliceXY->SetElement(2, 0, 0);
  rasToSliceXY->SetElement(2, 1, 0);
  rasToSliceXY->SetElement(2, 2, 0);
  this->WorldToSliceTransform->SetMatrix(rasToSliceXY.GetPointer());

  // Update slice plane (for distance computation)
  double normal[3];
  double origin[3];
  const double planeOrientation = 1.0; // +/-1: orientation of the normal
  for (int i = 0; i < 3; i++)
  {
    normal[i] = planeOrientation * sliceXYToRAS->GetElement(i, 2);
    origin[i] = sliceXYToRAS->GetElement(i, 3);
  }
  vtkMath::Normalize(normal);
  this->SlicePlane->SetNormal(normal);
  this->SlicePlane->SetOrigin(origin);
  this->SlicePlane->Modified();
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::UpdateViewScaleFactor()
{
  this->ViewScaleFactorMmPerPixel = 1.0;
  this->ScreenSizePixel = 1000.0;
  if (!this->Renderer || !this->Renderer->GetActiveCamera() || !this->GetSliceNode())
  {
    return;
  }

  const int* screenSize = this->Renderer->GetRenderWindow()->GetScreenSize();
  this->ScreenSizePixel = sqrt(screenSize[0] * screenSize[0] + screenSize[1] * screenSize[1]);

  vtkMatrix4x4* xyToSlice = this->GetSliceNode()->GetXYToSlice();
  this->ViewScaleFactorMmPerPixel = sqrt(xyToSlice->GetElement(0, 1) * xyToSlice->GetElement(0, 1) + xyToSlice->GetElement(1, 1) * xyToSlice->GetElement(1, 1));
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::UpdateControlPointSize()
{
  // Since we use parallel camera projection and the camera scale is 1.0,
  // the renderer coordinate system is the same as the display coordinate system, therefore
  // ControlPointSize is specified in pixels.
  if (this->MarkupsDisplayNode->GetUseGlyphScale())
  {
    // relative
    this->ControlPointSize = this->ScreenSizePixel * this->GetScreenScaleFactor() * this->MarkupsDisplayNode->GetGlyphScale() / 100.0;
  }
  else
  {
    // absolute
    this->ControlPointSize = this->MarkupsDisplayNode->GetGlyphSize() / this->ViewScaleFactorMmPerPixel;
  }
}

//----------------------------------------------------------------------
double vtkSlicerMarkupsWidgetRepresentation2D::GetMaximumControlPointPickingDistance2()
{
  double maximumControlPointPickingDistance = this->ControlPointSize / 2.0 + this->PickingTolerance * this->GetScreenScaleFactor();
  return maximumControlPointPickingDistance * maximumControlPointPickingDistance;
}

//----------------------------------------------------------------------
bool vtkSlicerMarkupsWidgetRepresentation2D::IsRepresentationIntersectingSlice(vtkPolyData* representation, const char* arrayName)
{
  if (!representation || !representation->GetPointData() || representation->GetNumberOfPoints() <= 0)
  {
    return false;
  }

  double sliceNormal_XY[4] = { 0.0, 0.0, 1.0, 0.0 };
  double sliceNormal_World[4] = { 0, 0, 1, 0 };
  vtkMatrix4x4* xyToRAS = this->GetSliceNode()->GetXYToRAS();
  xyToRAS->MultiplyPoint(sliceNormal_XY, sliceNormal_World);
  double sliceThicknessMm = vtkMath::Norm(sliceNormal_World);

  vtkDataArray* distanceArray = representation->GetPointData()->GetArray(arrayName);
  if (!distanceArray)
  {
    return false;
  }
  double* scalarRange = distanceArray->GetRange();

  // If the closest point on the line is further than a half-slice thickness, then hide the markup in 2D
  if (scalarRange[0] > 0.5 * sliceThicknessMm || scalarRange[1] < -0.5 * sliceThicknessMm)
  {
    return false;
  }
  return true;
}

//----------------------------------------------------------------------
int vtkSlicerMarkupsWidgetRepresentation2D::CalculatePropertiesLabelJitterOffset(int labelPosition)
{
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode)
  {
    return 0;
  }

  vtkMRMLScene* scene = markupsNode->GetScene();
  if (!scene)
  {
    return 0;
  }

  vtkMRMLMarkupsDisplayNode* displayNode = this->GetMarkupsDisplayNode();
  if (!displayNode)
  {
    return 0;
  }

  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  if (!sliceNode)
  {
    return 0;
  }

  // Collect all markup nodes with the same label position that are visible in this slice view
  std::vector<std::string> nodeIDs;
  std::vector<vtkMRMLNode*> markupNodes;
  scene->GetNodesByClass("vtkMRMLMarkupsNode", markupNodes);

  for (vtkMRMLNode* node : markupNodes)
  {
    vtkMRMLMarkupsNode* otherMarkupsNode = vtkMRMLMarkupsNode::SafeDownCast(node);
    if (!otherMarkupsNode || otherMarkupsNode->GetNumberOfDefinedControlPoints() == 0)
    {
      continue;
    }

    // Check all display nodes for this markup
    for (int i = 0; i < otherMarkupsNode->GetNumberOfDisplayNodes(); ++i)
    {
      vtkMRMLMarkupsDisplayNode* otherDisplayNode = vtkMRMLMarkupsDisplayNode::SafeDownCast(
        otherMarkupsNode->GetNthDisplayNode(i));

      if (!otherDisplayNode || !otherDisplayNode->GetVisibility() || !otherDisplayNode->GetPropertiesLabelVisibility())
      {
        continue;
      }

      // Check if visible in this view
      std::vector<std::string> viewNodeIDs = otherDisplayNode->GetViewNodeIDs();
      if (!viewNodeIDs.empty())
      {
        // If view node IDs are specified, check if this slice node is in the list
        std::string sliceNodeID = sliceNode->GetID();
        if (std::find(viewNodeIDs.begin(), viewNodeIDs.end(), sliceNodeID) == viewNodeIDs.end())
        {
          continue;
        }
      }

      // Check if this display node has the same label position
      if (otherDisplayNode->GetPropertiesLabelPosition() == labelPosition)
      {
        nodeIDs.push_back(otherMarkupsNode->GetID());
        break; // Only add each markup once
      }
    }
  }

  // Sort node IDs for consistent ordering
  std::sort(nodeIDs.begin(), nodeIDs.end());

  // Find this markup's index in the sorted list
  std::string currentNodeID = markupsNode->GetID();
  int index = 0;
  for (size_t i = 0; i < nodeIDs.size(); ++i)
  {
    if (nodeIDs[i] == currentNodeID)
    {
      index = static_cast<int>(i);
      break;
    }
  }

  // Calculate jitter offset
  // Spacing between labels (in pixels)
  int labelSpacing = 30;

  // Center the group of labels
  int totalOffset = index * labelSpacing;
  int groupCenterOffset = (static_cast<int>(nodeIDs.size()) - 1) * labelSpacing / 2;

  return totalOffset - groupCenterOffset;
}

//----------------------------------------------------------------------
bool vtkSlicerMarkupsWidgetRepresentation2D::UpdatePropertiesLabelPosition()
{
  vtkMRMLMarkupsDisplayNode* displayNode = this->GetMarkupsDisplayNode();
  if (!displayNode || !this->Renderer || !this->TextActor)
  {
    return true; // Render label with default behavior if no display node
  }

  int labelPosition = displayNode->GetPropertiesLabelPosition();
  if (labelPosition == vtkMRMLMarkupsDisplayNode::PropertiesLabelPositionDefault)
  {
    this->PropertiesLabelLineActor->SetVisibility(false);
    // Clear the line geometry to ensure it doesn't persist
    vtkNew<vtkPoints> emptyPoints;
    this->PropertiesLabelLinePolyData->SetPoints(emptyPoints);
    return true; // Use default positioning - render label normally
  }

  // Get viewport dimensions
  int* viewportSize = this->Renderer->GetSize();
  int margin = 10; // pixels from edge

  // Get current text actor position (in display coordinates) BEFORE we modify it
  vtkCoordinate* textPositionCoordinate = this->TextActor->GetPositionCoordinate();
  double* defaultDisplayPos = textPositionCoordinate->GetComputedDoubleDisplayValue(this->Renderer);

  // Store the default position to pass to the line update
  double savedDefaultPos[2] = { defaultDisplayPos[0], defaultDisplayPos[1] };

  // Calculate jitter offset based on other markups with the same label position
  int jitterOffset = this->CalculatePropertiesLabelJitterOffset(labelPosition);

  // Calculate position in normalized viewport coordinates (0-1 range)
  double normalizedMargin = static_cast<double>(margin) / viewportSize[0]; // approximate, using width
  double normalizedJitter = static_cast<double>(jitterOffset) / viewportSize[0]; // normalize the jitter
  double newNormalizedPos[2];

  // Get current normalized position
  double currentNormalizedX = savedDefaultPos[0] / viewportSize[0];
  double currentNormalizedY = savedDefaultPos[1] / viewportSize[1];

  vtkTextProperty* textProperty = this->TextActor->GetTextProperty();

  switch (labelPosition)
  {
    case vtkMRMLMarkupsDisplayNode::PropertiesLabelPositionLeft:
      newNormalizedPos[0] = normalizedMargin;
      newNormalizedPos[1] = currentNormalizedY + normalizedJitter;
      textProperty->SetJustificationToLeft();
      textProperty->SetVerticalJustificationToCentered();
      break;
    case vtkMRMLMarkupsDisplayNode::PropertiesLabelPositionRight:
      newNormalizedPos[0] = 1.0 - normalizedMargin;
      newNormalizedPos[1] = currentNormalizedY + normalizedJitter;
      textProperty->SetJustificationToRight();
      textProperty->SetVerticalJustificationToCentered();
      break;
    case vtkMRMLMarkupsDisplayNode::PropertiesLabelPositionTop:
      newNormalizedPos[0] = currentNormalizedX + normalizedJitter;
      newNormalizedPos[1] = 1.0 - normalizedMargin;
      textProperty->SetJustificationToCentered();
      textProperty->SetVerticalJustificationToTop();
      break;
    case vtkMRMLMarkupsDisplayNode::PropertiesLabelPositionBottom:
      newNormalizedPos[0] = currentNormalizedX + normalizedJitter;
      newNormalizedPos[1] = normalizedMargin;
      textProperty->SetJustificationToCentered();
      textProperty->SetVerticalJustificationToBottom();
      break;
    default:
      newNormalizedPos[0] = currentNormalizedX;
      newNormalizedPos[1] = currentNormalizedY;
  }

  // Convert back to display coordinates for SetDisplayPosition
  double newDisplayPos[2];
  newDisplayPos[0] = newNormalizedPos[0] * viewportSize[0];
  newDisplayPos[1] = newNormalizedPos[1] * viewportSize[1];

  this->TextActor->SetDisplayPosition(
    static_cast<int>(newDisplayPos[0]),
    static_cast<int>(newDisplayPos[1]));

  // Update line if needed
  if (displayNode->GetPropertiesLabelLineVisibility())
  {
    this->UpdatePropertiesLabelLine(savedDefaultPos, newDisplayPos, labelPosition);
  }
  else
  {
    this->PropertiesLabelLineActor->SetVisibility(false);
  }

  return true; // Render the label
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation2D::UpdatePropertiesLabelLine(const double defaultDisplayPosition[2], const double newDisplayPosition[2], int labelPosition)
{
  vtkMRMLMarkupsDisplayNode* displayNode = this->GetMarkupsDisplayNode();
  if (!displayNode || !this->Renderer || !this->TextActor)
  {
    this->PropertiesLabelLineActor->SetVisibility(false);
    return;
  }

  // Get text bounds in display coordinates
  double textSize[2];
  this->TextActor->GetSize(this->Renderer, textSize);

  // Calculate the underline position (below the text)
  double underlineY = newDisplayPosition[1];
  double underlineOffset = 3.0; // pixels below text baseline

  // Adjust underline Y position based on vertical justification
  vtkTextProperty* textProperty = this->TextActor->GetTextProperty();
  int verticalJustification = textProperty->GetVerticalJustification();
  if (verticalJustification == VTK_TEXT_TOP)
  {
    underlineY -= textSize[1] + underlineOffset;
  }
  else if (verticalJustification == VTK_TEXT_CENTERED)
  {
    underlineY -= textSize[1] / 2.0 + underlineOffset;
  }
  else // VTK_TEXT_BOTTOM
  {
    underlineY -= underlineOffset;
  }

  // Calculate underline endpoints based on label position
  double underlineStart[2], underlineEnd[2];
  double underlineLength = textSize[0] * 0.8; // 80% of text width

  switch (labelPosition)
  {
    case vtkMRMLMarkupsDisplayNode::PropertiesLabelPositionLeft:
    case vtkMRMLMarkupsDisplayNode::PropertiesLabelPositionRight:
      // Horizontal underline
      if (textProperty->GetJustification() == VTK_TEXT_LEFT)
      {
        underlineStart[0] = newDisplayPosition[0];
        underlineEnd[0] = newDisplayPosition[0] + underlineLength;
      }
      else if (textProperty->GetJustification() == VTK_TEXT_RIGHT)
      {
        underlineStart[0] = newDisplayPosition[0] - underlineLength;
        underlineEnd[0] = newDisplayPosition[0];
      }
      else // VTK_TEXT_CENTERED
      {
        underlineStart[0] = newDisplayPosition[0] - underlineLength / 2.0;
        underlineEnd[0] = newDisplayPosition[0] + underlineLength / 2.0;
      }
      underlineStart[1] = underlineY;
      underlineEnd[1] = underlineY;
      break;

    case vtkMRMLMarkupsDisplayNode::PropertiesLabelPositionTop:
    case vtkMRMLMarkupsDisplayNode::PropertiesLabelPositionBottom:
      // For top/bottom positions, use center of text for underline
      underlineStart[0] = newDisplayPosition[0] - underlineLength / 2.0;
      underlineEnd[0] = newDisplayPosition[0] + underlineLength / 2.0;
      underlineStart[1] = underlineY;
      underlineEnd[1] = underlineY;
      break;

    default:
      underlineStart[0] = newDisplayPosition[0];
      underlineStart[1] = underlineY;
      underlineEnd[0] = newDisplayPosition[0] + underlineLength;
      underlineEnd[1] = underlineY;
  }

  // Get the center of the underline for the connecting line
  double underlineCenter[2];
  underlineCenter[0] = (underlineStart[0] + underlineEnd[0]) / 2.0;
  underlineCenter[1] = underlineY;

  // Create polydata with underline and connecting line in display coordinates
  vtkNew<vtkPoints> points;
  points->InsertNextPoint(underlineStart[0], underlineStart[1], 0);
  points->InsertNextPoint(underlineEnd[0], underlineEnd[1], 0);
  points->InsertNextPoint(underlineCenter[0], underlineCenter[1], 0);
  points->InsertNextPoint(defaultDisplayPosition[0], defaultDisplayPosition[1], 0);

  vtkNew<vtkCellArray> lines;
  // Underline
  lines->InsertNextCell(2);
  lines->InsertCellPoint(0);
  lines->InsertCellPoint(1);
  // Connecting line from underline center to default position
  lines->InsertNextCell(2);
  lines->InsertCellPoint(2);
  lines->InsertCellPoint(3);

  this->PropertiesLabelLinePolyData->SetPoints(points);
  this->PropertiesLabelLinePolyData->SetLines(lines);

  // Set line appearance
  int controlPointType = this->GetAllControlPointsSelected() ?
    vtkSlicerMarkupsWidgetRepresentation::Selected :
    vtkSlicerMarkupsWidgetRepresentation::Unselected;
  this->PropertiesLabelLineActor->SetProperty(
    this->GetControlPointsPipeline(controlPointType)->Property);

  this->PropertiesLabelLineActor->SetVisibility(true);
}
