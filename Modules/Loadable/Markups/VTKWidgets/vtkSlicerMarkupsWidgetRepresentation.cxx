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
#include "vtkActor2DCollection.h"
#include "vtkAppendPolyData.h"
#include "vtkArrayCalculator.h"
#include "vtkArcSource.h"
#include "vtkArrowSource.h"
#include "vtkConeSource.h"
#include "vtkSlicerMarkupsWidgetRepresentation.h"
#include "vtkCamera.h"
#include "vtkDoubleArray.h"
#include "vtkFocalPlanePointPlacer.h"
#include "vtkGlyph3D.h"
#include "vtkLine.h"
#include "vtkLookupTable.h"
#include "vtkMarkupsGlyphSource2D.h"
#include "vtkPointData.h"
#include "vtkPointSetToLabelHierarchy.h"
#include "vtkPolyDataMapper2D.h"
#include "vtkProperty2D.h"
#include "vtkRegularPolygonSource.h"
#include "vtkRenderer.h"
#include "vtkSphereSource.h"
#include "vtkStringArray.h"
#include "vtkTextActor.h"
#include "vtkTextProperty.h"
#include "vtkTensorGlyph.h"
#include "vtkTransform.h"
#include "vtkTransformPolyDataFilter.h"

#include "vtkLineSource.h"

// MRML includes
#include <vtkMRMLFolderDisplayNode.h>
#include <vtkMRMLInteractionEventData.h>
#include <vtkMRMLViewNode.h>

//----------------------------------------------------------------------
vtkSlicerMarkupsWidgetRepresentation::ControlPointsPipeline::ControlPointsPipeline()
{
  this->TextProperty = vtkSmartPointer<vtkTextProperty>::New();
  this->TextProperty->SetFontSize(15);
  this->TextProperty->SetFontFamily(vtkTextProperty::GetFontFamilyFromString("Arial"));
  this->TextProperty->SetColor(0.4, 1.0, 1.0);
  this->TextProperty->SetOpacity(1.);

  this->ControlPoints = vtkSmartPointer<vtkPoints>::New();
  this->ControlPoints->Allocate(100);
  this->ControlPoints->SetNumberOfPoints(1);
  this->ControlPoints->SetPoint(0, 0.0, 0.0, 0.0);

  vtkNew<vtkDoubleArray> controlPointNormals;
  controlPointNormals->SetNumberOfComponents(3);
  controlPointNormals->Allocate(100);
  controlPointNormals->SetNumberOfTuples(1);
  double n[3] = { 0, 0, 0 };
  controlPointNormals->SetTuple(0, n);

  this->ControlPointsPolyData = vtkSmartPointer<vtkPolyData>::New();
  this->ControlPointsPolyData->SetPoints(this->ControlPoints);
  this->ControlPointsPolyData->GetPointData()->SetNormals(controlPointNormals);

  this->LabelControlPoints = vtkSmartPointer<vtkPoints>::New();
  this->LabelControlPoints->Allocate(100);
  this->LabelControlPoints->SetNumberOfPoints(1);
  this->LabelControlPoints->SetPoint(0, 0.0, 0.0, 0.0);

  vtkNew<vtkDoubleArray> labelNormals;
  labelNormals->SetNumberOfComponents(3);
  labelNormals->Allocate(100);
  labelNormals->SetNumberOfTuples(1);
  labelNormals->SetTuple(0, n);

  this->LabelControlPointsPolyData = vtkSmartPointer<vtkPolyData>::New();
  this->LabelControlPointsPolyData->SetPoints(this->LabelControlPoints);
  this->LabelControlPointsPolyData->GetPointData()->SetNormals(labelNormals);

  this->Labels = vtkSmartPointer<vtkStringArray>::New();
  this->Labels->SetName("labels");
  this->Labels->Allocate(100);
  this->Labels->SetNumberOfValues(1);
  this->Labels->SetValue(0, "F");
  this->LabelsPriority = vtkSmartPointer<vtkStringArray>::New();
  this->LabelsPriority->SetName("priority");
  this->LabelsPriority->Allocate(100);
  this->LabelsPriority->SetNumberOfValues(1);
  this->LabelsPriority->SetValue(0, "1");
  this->LabelControlPointsPolyData->GetPointData()->AddArray(this->Labels);
  this->LabelControlPointsPolyData->GetPointData()->AddArray(this->LabelsPriority);
  this->PointSetToLabelHierarchyFilter = vtkSmartPointer<vtkPointSetToLabelHierarchy>::New();
  this->PointSetToLabelHierarchyFilter->SetTextProperty(this->TextProperty);
  this->PointSetToLabelHierarchyFilter->SetLabelArrayName("labels");
  this->PointSetToLabelHierarchyFilter->SetPriorityArrayName("priority");
  this->PointSetToLabelHierarchyFilter->SetInputData(this->LabelControlPointsPolyData);

  this->GlyphSource2D = vtkSmartPointer<vtkMarkupsGlyphSource2D>::New();

  this->GlyphSourceSphere = vtkSmartPointer<vtkSphereSource>::New();
  this->GlyphSourceSphere->SetRadius(0.5);
};

//----------------------------------------------------------------------
vtkSlicerMarkupsWidgetRepresentation::ControlPointsPipeline::~ControlPointsPipeline()
= default;

//----------------------------------------------------------------------
vtkSlicerMarkupsWidgetRepresentation::vtkSlicerMarkupsWidgetRepresentation()
{
  this->ViewScaleFactorMmPerPixel = 1.0;
  this->ScreenSizePixel = 1000;

  this->ControlPointSize = 3.0;
  this->NeedToRender             = false;
  this->ClosedLoop               = 0;

  this->TextActor = vtkSmartPointer<vtkTextActor>::New();
  this->TextActor->SetInput("");

  this->PointPlacer = vtkSmartPointer<vtkFocalPlanePointPlacer>::New();

  for (int i = 0; i<NumberOfControlPointTypes; i++)
    {
    this->ControlPoints[i] = nullptr;
    }

  this->AlwaysOnTop = false;

  this->InteractionPipeline = nullptr;
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::SetupInteractionPipeline()
{
  this->InteractionPipeline = new MarkupsInteractionPipeline(this);
  this->InteractionPipeline->InitializePipeline();
}

//----------------------------------------------------------------------
vtkSlicerMarkupsWidgetRepresentation::~vtkSlicerMarkupsWidgetRepresentation()
{
  for (int i=0; i<NumberOfControlPointTypes; i++)
    {
    delete this->ControlPoints[i];
    this->ControlPoints[i] = nullptr;
    }
  // Force deleting variables to prevent circular dependency keeping objects alive
  this->PointPlacer = nullptr;

  delete this->InteractionPipeline;
  this->InteractionPipeline = nullptr;
}

//----------------------------------------------------------------------
int vtkSlicerMarkupsWidgetRepresentation::GetNumberOfControlPoints()
{
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode)
    {
    return 0;
    }
  return markupsNode->GetNumberOfControlPoints();
}

//----------------------------------------------------------------------
// The display position for a given world position must be re-computed
// from the world positions... It should not be queried from the renderer
// whose camera position may have changed
int vtkSlicerMarkupsWidgetRepresentation::GetNthControlPointDisplayPosition(int n, double displayPos[2])
{
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode || n < 0 || n >= markupsNode->GetNumberOfControlPoints())
    {
    return 0;
    }

  double pos[4] = { 0.0, 0.0, 0.0, 1.0 };
  markupsNode->TransformPointToWorld(this->GetNthControlPoint(n)->Position, pos);

  this->Renderer->SetWorldPoint(pos);
  this->Renderer->WorldToDisplay();
  this->Renderer->GetDisplayPoint(pos);

  displayPos[0] = pos[0];
  displayPos[1] = pos[1];
  return 1;
}

//----------------------------------------------------------------------
vtkMRMLMarkupsNode::ControlPoint* vtkSlicerMarkupsWidgetRepresentation::GetNthControlPoint(int n)
{
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode || n < 0 || n >= markupsNode->GetNumberOfControlPoints())
    {
    return nullptr;
    }
  return markupsNode->GetNthControlPoint(n);
}

//----------------------------------------------------------------------
int vtkSlicerMarkupsWidgetRepresentation::FindClosestPointOnWidget(
  const int displayPos[2], double closestWorldPos[3], int *idx)
{
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode)
    {
    return 0;
    }

  // Make a line out of this viewing ray
  double p1[4] = { 0.0, 0.0, 0.0, 1.0 };
  double tmp1[4] = { static_cast<double>(displayPos[0]), static_cast<double>(displayPos[1]), 0.0, 1.0 };
  this->Renderer->SetDisplayPoint(tmp1);
  this->Renderer->DisplayToWorld();
  this->Renderer->GetWorldPoint(p1);

  double p2[4] = { 0.0, 0.0, 0.0, 1.0 };
  tmp1[2] = 1.0;
  this->Renderer->SetDisplayPoint(tmp1);
  this->Renderer->DisplayToWorld();
  this->Renderer->GetWorldPoint(p2);

  double closestDistance2 = VTK_DOUBLE_MAX;
  int closestNode = 0;

  // compute a world tolerance based on pixel
  // tolerance on the focal plane
  double fp[4] = { 0.0, 0.0, 0.0, 1.0 };
  this->Renderer->GetActiveCamera()->GetFocalPoint(fp);
  this->Renderer->SetWorldPoint(fp);
  this->Renderer->WorldToDisplay();
  this->Renderer->GetDisplayPoint(tmp1);

  tmp1[0] = 0;
  tmp1[1] = 0;
  double tmp2[4] = { 0.0, 0.0, 0.0, 1.0 };
  this->Renderer->SetDisplayPoint(tmp1);
  this->Renderer->DisplayToWorld();
  this->Renderer->GetWorldPoint(tmp2);

  tmp1[0] = this->PickingTolerance * this->ScreenScaleFactor;
  this->Renderer->SetDisplayPoint(tmp1);
  this->Renderer->DisplayToWorld();
  this->Renderer->GetWorldPoint(tmp1);

  double wt2 = vtkMath::Distance2BetweenPoints(tmp1, tmp2);


  // Now loop through all lines and look for closest one within tolerance
  double p3[4] = {0.0, 0.0, 0.0, 1.0};
  double p4[4] = {0.0, 0.0, 0.0, 1.0};
  vtkPoints* curvePointsWorld = this->GetMarkupsNode()->GetCurvePointsWorld();
  vtkIdType numberOfPoints = curvePointsWorld->GetNumberOfPoints();
  for(vtkIdType i = 0; i < numberOfPoints; i++)
    {
    curvePointsWorld->GetPoint(i, p3);
    if (i + 1 < numberOfPoints)
      {
      curvePointsWorld->GetPoint(i + 1, p4);
      }
    else
      {
      if (!this->ClosedLoop)
        {
        continue;
        }
      curvePointsWorld->GetPoint(0, p4);
      }

    // Now we have the four points - check closest intersection
    double u, v;
    if (vtkLine::Intersection(p1, p2, p3, p4, u, v))
      {
      double p5[3], p6[3];
      p5[0] = p1[0] + u*(p2[0]-p1[0]);
      p5[1] = p1[1] + u*(p2[1]-p1[1]);
      p5[2] = p1[2] + u*(p2[2]-p1[2]);

      p6[0] = p3[0] + v*(p4[0]-p3[0]);
      p6[1] = p3[1] + v*(p4[1]-p3[1]);
      p6[2] = p3[2] + v*(p4[2]-p3[2]);

      double d = vtkMath::Distance2BetweenPoints(p5, p6);

      if (d < wt2 && d < closestDistance2)
        {
        closestWorldPos[0] = p6[0];
        closestWorldPos[1] = p6[1];
        closestWorldPos[2] = p6[2];
        closestDistance2 = d;
        closestNode = static_cast<int>(i);
        }
      }
    else
      {
      double d = vtkLine::DistanceToLine(p3, p1, p2);
      if (d < wt2 && d < closestDistance2)
        {
        closestWorldPos[0] = p3[0];
        closestWorldPos[1] = p3[1];
        closestWorldPos[2] = p3[2];
        closestDistance2 = d;
        closestNode = static_cast<int>(i);
        }

      d = vtkLine::DistanceToLine(p4, p1, p2);
      if (d < wt2 && d < closestDistance2)
        {
        closestWorldPos[0] = p4[0];
        closestWorldPos[1] = p4[1];
        closestWorldPos[2] = p4[2];
        closestDistance2 = d;
        closestNode = static_cast<int>(i);
        }
      }
    }

  if (closestDistance2 < VTK_DOUBLE_MAX)
    {
    if (closestNode < markupsNode->GetNumberOfControlPoints() -1)
      {
      *idx = closestNode+1;
      return 1;
      }
    else if (this->ClosedLoop)
      {
      *idx = 0;
      return 1;
      }
    }

  return 0;
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::UpdateCenter()
{
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode || markupsNode->GetNumberOfControlPoints() < 1)
    {
    return;
    }
  double centerWorldPos[3] = { 0.0 };

  for (int i = 0; i < markupsNode->GetNumberOfControlPoints(); i++)
    {
    double p[4];
    markupsNode->GetNthControlPointPositionWorld(i, p);
    centerWorldPos[0] += p[0];
    centerWorldPos[1] += p[1];
    centerWorldPos[2] += p[2];
    }
  double inv_N = 1. / static_cast< double >(markupsNode->GetNumberOfControlPoints());
  centerWorldPos[0] *= inv_N;
  centerWorldPos[1] *= inv_N;
  centerWorldPos[2] *= inv_N;
  markupsNode->SetCenterPositionFromPointer(centerWorldPos);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::SetMarkupsDisplayNode(vtkMRMLMarkupsDisplayNode *markupsDisplayNode)
{
  if (this->MarkupsDisplayNode == markupsDisplayNode)
    {
    return;
    }

  this->MarkupsDisplayNode = markupsDisplayNode;

  vtkMRMLMarkupsNode* markupsNode = nullptr;
  if (this->MarkupsDisplayNode)
    {
    markupsNode = vtkMRMLMarkupsNode::SafeDownCast(this->MarkupsDisplayNode->GetDisplayableNode());
    }
  this->SetMarkupsNode(markupsNode);
}

//----------------------------------------------------------------------
vtkMRMLMarkupsDisplayNode *vtkSlicerMarkupsWidgetRepresentation::GetMarkupsDisplayNode()
{
  return this->MarkupsDisplayNode;
}

//----------------------------------------------------------------------
vtkMRMLMarkupsNode *vtkSlicerMarkupsWidgetRepresentation::GetMarkupsNode()
{
  if (!this->MarkupsDisplayNode)
    {
    return nullptr;
    }
  return vtkMRMLMarkupsNode::SafeDownCast(this->MarkupsDisplayNode->GetDisplayableNode());
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::SetMarkupsNode(vtkMRMLMarkupsNode *markupsNode)
{
  this->MarkupsNode = markupsNode;
}

//-----------------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::PrintSelf(ostream& os,
                                                      vtkIndent indent)
{
  //Superclass typedef defined in vtkTypeMacro() found in vtkSetGet.h
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Point Placer: " << this->PointPlacer << "\n";
}

//-----------------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::CanInteract(
  vtkMRMLInteractionEventData* vtkNotUsed(interactionEventData),
  int &foundComponentType, int &vtkNotUsed(foundComponentIndex), double &vtkNotUsed(closestDistance2))
{
  foundComponentType = vtkMRMLMarkupsDisplayNode::ComponentNone;
}

//----------------------------------------------------------------------
bool vtkSlicerMarkupsWidgetRepresentation::GetTransformationReferencePoint(double referencePointWorld[3])
{
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode)
    {
    return false;
    }

  this->UpdateCenter();
  markupsNode->GetCenterPosition(referencePointWorld);
  return true;
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::BuildLine(vtkPolyData* linePolyData, bool displayPosition)
{
  vtkNew<vtkPoints> points;
  vtkNew<vtkCellArray> line;

  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode)
    {
    linePolyData->SetPoints(points);
    linePolyData->SetLines(line);
    return;
    }
  int numberOfControlPoints = markupsNode->GetNumberOfControlPoints();
  vtkIdType numberOfLines = numberOfControlPoints - 1;
  bool loop = (markupsNode->GetCurveClosed() && numberOfControlPoints > 2);
  if (loop)
    {
    numberOfLines++;
    }
  if (numberOfLines <= 0)
    {
    return;
    }

  double pos[3] = { 0.0 };
  vtkIdType index = 0;
  line->InsertNextCell(numberOfLines+1);

  for (int i = 0; i < numberOfControlPoints; i++)
    {
    // Add the node
    if (displayPosition)
      {
      this->GetNthControlPointDisplayPosition(i, pos);
      }
    else
      {
      markupsNode->GetNthControlPointPositionWorld(i, pos);
      }
    points->InsertNextPoint(pos);
    line->InsertCellPoint(i);
    index++;
    }

  if (loop)
    {
    if (displayPosition)
      {
      this->GetNthControlPointDisplayPosition(0, pos);
      }
    else
      {
      markupsNode->GetNthControlPointPositionWorld(0, pos);
      }
    points->InsertPoint(index, pos);
    line->InsertCellPoint(0);
    }

  linePolyData->SetPoints(points);
  linePolyData->SetLines(line);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::UpdateFromMRML(
    vtkMRMLNode* node, unsigned long event, void *vtkNotUsed(callData))
{
  if (!this->InteractionPipeline)
    {
    this->SetupInteractionPipeline();
    }

  if (!event || event == vtkMRMLTransformableNode::TransformModifiedEvent)
    {
    this->MarkupsTransformModifiedTime.Modified();
    }

  if (!event || event == vtkMRMLDisplayableNode::DisplayModifiedEvent)
    {
    // Update MRML data node from display node
    vtkMRMLMarkupsNode* markupsNode = nullptr;
    if (this->MarkupsDisplayNode)
      {
      markupsNode = vtkMRMLMarkupsNode::SafeDownCast(this->MarkupsDisplayNode->GetDisplayableNode());
      }
    this->SetMarkupsNode(markupsNode);
    }

  this->TextActor->SetVisibility(this->MarkupsDisplayNode->GetPropertiesLabelVisibility());

  this->NeedToRenderOn(); // TODO: to improve performance, call this only if it is actually needed

  if (this->InteractionPipeline)
    {
    this->UpdateInteractionPipeline();
    }
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::UpdateInteractionPipeline()
{
  bool selected = false;
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (markupsNode)
    {
    selected = markupsNode->GetSelected();
    }
  this->InteractionPipeline->Actor->SetVisibility(false);

  double origin[3] = { 0 };
  markupsNode->GetCenterPositionWorld(origin);

  vtkNew<vtkTransform> transform;
  transform->Translate(origin);
  this->InteractionPipeline->ModelToWorldTransform->SetTransform(transform);
}

//----------------------------------------------------------------------
bool vtkSlicerMarkupsWidgetRepresentation::GetAllControlPointsVisible()
{
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode)
    {
    return false;
    }

  for (int controlPointIndex = 0; controlPointIndex < markupsNode->GetNumberOfControlPoints(); controlPointIndex++)
    {
    if (!markupsNode->GetNthControlPointVisibility(controlPointIndex))
      {
      return false;
      }
    }
  return true;
}

//----------------------------------------------------------------------
bool vtkSlicerMarkupsWidgetRepresentation::GetAllControlPointsSelected()
{
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode)
    {
    return false;
    }

  for (int controlPointIndex = 0; controlPointIndex < markupsNode->GetNumberOfControlPoints(); controlPointIndex++)
    {
    if (!markupsNode->GetNthControlPointSelected(controlPointIndex))
      {
      return false;
      }
    }
  return true;
}

//----------------------------------------------------------------------
double* vtkSlicerMarkupsWidgetRepresentation::GetWidgetColor(int controlPointType)
{
  static double invalidColor[3] = { 0.5, 0.5, 0.5 }; // gray
  static double activeColor[3] = { 0.4, 1.0, 0. }; // bright green
  static double color[3];

  if (!this->MarkupsDisplayNode)
    {
    return invalidColor;
    }

  // If a folder is overriding display properties then return the color defined by the folder
  if (this->MarkupsDisplayNode->GetFolderDisplayOverrideAllowed())
    {
    vtkMRMLDisplayableNode* displayableNode = this->MarkupsDisplayNode->GetDisplayableNode();
    vtkMRMLDisplayNode* overrideHierarchyDisplayNode =
      vtkMRMLFolderDisplayNode::GetOverridingHierarchyDisplayNode(displayableNode);
    if (overrideHierarchyDisplayNode)
      {
      overrideHierarchyDisplayNode->GetColor(color);
      return color;
      }
    }

  switch (controlPointType)
    {
    case Unselected:
      this->MarkupsDisplayNode->GetColor(color);
      break;
    case Selected:
      this->MarkupsDisplayNode->GetSelectedColor(color);
      break;
    case Active: return activeColor;
    case Project:
      if (this->MarkupsDisplayNode->GetSliceProjectionUseFiducialColor())
        {
        if (this->GetAllControlPointsSelected())
          {
          this->MarkupsDisplayNode->GetSelectedColor(color);
          }
        else
          {
          this->MarkupsDisplayNode->GetColor(color);
          }
        }
      else
        {
        this->MarkupsDisplayNode->GetSliceProjectionColor(color);
        }
      break;
    case ProjectBack:
      if (this->MarkupsDisplayNode->GetSliceProjectionUseFiducialColor())
        {
        if (this->GetAllControlPointsSelected())
          {
          this->MarkupsDisplayNode->GetSelectedColor(color);
          }
        else
          {
          this->MarkupsDisplayNode->GetColor(color);
          }
        }
      else
        {
        this->MarkupsDisplayNode->GetSliceProjectionColor(color);
        }
      for (int i = 0; i < 3; i++)
        {
        if (fabs(color[1] - 1.) > 0.001)
          {
          color[i] = 1. - color[i];
          }
        }
      break;
    default:
      return invalidColor;
    }

  return color;
}

//----------------------------------------------------------------------
vtkPointPlacer* vtkSlicerMarkupsWidgetRepresentation::GetPointPlacer()
{
  return this->PointPlacer;
}

//----------------------------------------------------------------------
vtkPolyData* vtkSlicerMarkupsWidgetRepresentation::GetControlPointsPolyData(int controlPointType)
{
  if (controlPointType < 0 || controlPointType >= NumberOfControlPointTypes)
    {
    vtkErrorMacro("vtkSlicerMarkupsWidgetRepresentation::GetControlPointsPolyData failed: invalid controlPointType: " << controlPointType)
    return nullptr;
    }
  return this->ControlPoints[controlPointType]->ControlPointsPolyData;
}

//----------------------------------------------------------------------
vtkPolyData* vtkSlicerMarkupsWidgetRepresentation::GetLabelControlPointsPolyData(int controlPointType)
{
  if (controlPointType < 0 || controlPointType >= NumberOfControlPointTypes)
    {
    vtkErrorMacro("vtkSlicerMarkupsWidgetRepresentation::GetLabelControlPointsPolyData failed: invalid controlPointType: " << controlPointType)
    return nullptr;
    }
  return this->ControlPoints[controlPointType]->LabelControlPointsPolyData;
}

//----------------------------------------------------------------------
vtkStringArray* vtkSlicerMarkupsWidgetRepresentation::GetLabels(int controlPointType)
{
  if (controlPointType < 0 || controlPointType >= NumberOfControlPointTypes)
    {
    vtkErrorMacro("vtkSlicerMarkupsWidgetRepresentation::GetControlPointsPolyData failed: invalid controlPointType: " << controlPointType)
    return nullptr;
    }
  return this->ControlPoints[controlPointType]->Labels;
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::GetActors(vtkPropCollection* pc)
{
  if (this->InteractionPipeline)
    {
    this->InteractionPipeline->Actor->GetActors(pc);
    }
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::ReleaseGraphicsResources(vtkWindow* window)
{
  if (this->InteractionPipeline)
    {
    this->InteractionPipeline->Actor->ReleaseGraphicsResources(window);
    }
}

//----------------------------------------------------------------------
int vtkSlicerMarkupsWidgetRepresentation::RenderOverlay(vtkViewport* viewport)
{
  int count = 0;
  if (this->InteractionPipeline && this->InteractionPipeline->Actor->GetVisibility())
    {
    count += this->InteractionPipeline->Actor->RenderOverlay(viewport);
    }
  return count;
}

//----------------------------------------------------------------------
int vtkSlicerMarkupsWidgetRepresentation::RenderOpaqueGeometry(vtkViewport* viewport)
{
  int count = 0;
  if (this->InteractionPipeline && this->InteractionPipeline->Actor->GetVisibility())
    {
    this->InteractionPipeline->UpdateHandleColors();
    double interactionWidgetScale = 7.0 * this->ControlPointSize;
    this->InteractionPipeline->SetWidgetScale(interactionWidgetScale);
    count += this->InteractionPipeline->Actor->RenderOpaqueGeometry(viewport);
    }
  return count;
}

//----------------------------------------------------------------------
int vtkSlicerMarkupsWidgetRepresentation::RenderTranslucentPolygonalGeometry(vtkViewport* viewport)
{
  int count = 0;
  if (this->InteractionPipeline && this->InteractionPipeline->Actor->GetVisibility())
    {
    count += this->InteractionPipeline->Actor->RenderTranslucentPolygonalGeometry(viewport);
    }
  return count;
}

//----------------------------------------------------------------------
vtkTypeBool vtkSlicerMarkupsWidgetRepresentation::HasTranslucentPolygonalGeometry()
{
  if (this->InteractionPipeline && this->InteractionPipeline->Actor->GetVisibility() &&
    this->InteractionPipeline->Actor->HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  return false;
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::GetInteractionAxis(int index, double axis[3])
{
  this->InteractionPipeline->GetInteractionAxis(index, axis);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::GetInteractionOrigin(double origin[3])
{
  this->InteractionPipeline->GetInteractionOrigin(origin);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::GetInteractionModelToWorldMatrix(vtkTransform* matrix)
{
  this->InteractionPipeline->GetInteractionModelToWorldMatrix(matrix);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::GetInteractionHandleVector(int type, int index, double axis[3])
{
  vtkPolyData* handles = nullptr;
  if (type == vtkMRMLMarkupsDisplayNode::ComponentRotationHandle)
    {
    handles = this->InteractionPipeline->RotationHandlePoints;
    }

  if (!handles)
    {
    vtkErrorMacro("Could not find handle!");
    return;
    }

  if (index  < 0 || index >= handles->GetNumberOfPoints())
    {
    vtkErrorMacro("Handle index out of range!");
    return;
    }

  handles->GetPoint(index, axis);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::GetInteractionHandleVectorWorld(int type, int index, double axis[3])
{
  this->GetInteractionHandleVector(type, index, axis);
  double origin[3] = { 0 };
  this->InteractionPipeline->ModelToWorldTransform->GetTransform()->TransformVectorAtPoint(origin, axis, axis);
}

//----------------------------------------------------------------------
vtkSlicerMarkupsWidgetRepresentation::MarkupsInteractionPipeline::MarkupsInteractionPipeline(vtkMRMLAbstractWidgetRepresentation* representation)
{
  this->Representation = representation;

  double handleRadius = 0.125;
  this->AxisRotationHandleSource = vtkSmartPointer<vtkSphereSource>::New();
  this->AxisRotationHandleSource->SetRadius(handleRadius);

  this->AxisRotationArcSource = vtkSmartPointer<vtkArcSource>::New();
  this->AxisRotationArcSource->SetAngle(90);
  this->AxisRotationArcSource->SetCenter(-1, 0, 0);
  this->AxisRotationArcSource->SetPoint1(1.0 / sqrt(2) - 1.0, -1.0/sqrt(2), 0);
  this->AxisRotationArcSource->SetPoint2(1.0 / sqrt(2) - 1.0,  1.0/sqrt(2), 0);
  this->AxisRotationArcSource->SetResolution(6);

  vtkNew<vtkLineSource> line;
  line->SetPoint1(100, 0, 0);
  line->SetPoint2(-100, 0, 0);

  this->AxisRotationGlyphSource = vtkSmartPointer <vtkAppendPolyData>::New();
  this->AxisRotationGlyphSource->AddInputConnection(this->AxisRotationHandleSource->GetOutputPort());
  this->AxisRotationGlyphSource->AddInputConnection(this->AxisRotationArcSource->GetOutputPort());
  //this->AxisRotationGlyphSource->AddInputConnection(line->GetOutputPort());

  this->AxisTranslationGlyphSource = vtkSmartPointer<vtkArrowSource>::New();
  this->AxisTranslationGlyphSource->SetTipRadius(handleRadius);
  this->AxisTranslationGlyphSource->SetTipLength(2 * handleRadius);
  this->AxisTranslationGlyphSource->InvertOn();

  vtkNew<vtkTransform> translationGlyphTransformer;
  translationGlyphTransformer->Translate(handleRadius, 0, 0);
  translationGlyphTransformer->RotateY(180);

  this->AxisTranslationGlyphTransformer = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->AxisTranslationGlyphTransformer->SetTransform(translationGlyphTransformer);
  this->AxisTranslationGlyphTransformer->SetInputConnection(this->AxisTranslationGlyphSource->GetOutputPort());

  this->RotationHandlePoints = vtkSmartPointer<vtkPolyData>::New();
  this->TranslationHandlePoints = vtkSmartPointer<vtkPolyData>::New();

  this->RotationScaleTransform = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->RotationScaleTransform->SetInputData(this->RotationHandlePoints);
  this->RotationScaleTransform->SetTransform(vtkNew<vtkTransform>());

  this->TranslationScaleTransform = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->TranslationScaleTransform->SetInputData(this->TranslationHandlePoints);
  this->TranslationScaleTransform->SetTransform(vtkNew<vtkTransform>());

  this->AxisRotationGlypher = vtkSmartPointer<vtkTensorGlyph>::New();
  this->AxisRotationGlypher->SetInputConnection(this->RotationScaleTransform->GetOutputPort());
  this->AxisRotationGlypher->SetSourceConnection(this->AxisRotationGlyphSource->GetOutputPort());
  this->AxisRotationGlypher->SetColorModeToScalars();
  this->AxisRotationGlypher->ScalingOff();
  this->AxisRotationGlypher->ColorGlyphsOn();
  this->AxisRotationGlypher->ExtractEigenvaluesOff();
  this->AxisRotationGlypher->SetInputArrayToProcess(0, 0, 0, 0, "orientation");
  this->AxisRotationGlypher->SetInputArrayToProcess(1, 0, 0, 0, "color"); // Color array

  this->AxisTranslationGlypher = vtkSmartPointer<vtkTensorGlyph>::New();
  this->AxisTranslationGlypher->SetInputConnection(this->TranslationScaleTransform->GetOutputPort());
  this->AxisTranslationGlypher->SetSourceConnection(this->AxisTranslationGlyphTransformer->GetOutputPort());
  this->AxisTranslationGlypher->SetColorModeToScalars();
  this->AxisTranslationGlypher->ScalingOff();
  this->AxisTranslationGlypher->ColorGlyphsOn();
  this->AxisTranslationGlypher->ExtractEigenvaluesOff();
  this->AxisTranslationGlypher->SetInputArrayToProcess(0, 0, 0, 0, "orientation"); // Orientation direction array
  this->AxisTranslationGlypher->SetInputArrayToProcess(1, 0, 0, 0, "color"); // Color array

  this->Append = vtkSmartPointer<vtkAppendPolyData>::New();
  this->Append->AddInputConnection(this->AxisRotationGlypher->GetOutputPort());
  this->Append->AddInputConnection(this->AxisTranslationGlypher->GetOutputPort());

  this->ModelToWorldTransform = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->ModelToWorldTransform->SetInputConnection(this->Append->GetOutputPort());
  this->ModelToWorldTransform->SetTransform(vtkNew<vtkTransform>());

  this->ColorTable = vtkSmartPointer<vtkLookupTable>::New();

  vtkNew<vtkCoordinate> coordinate;
  coordinate->SetCoordinateSystemToWorld();

  this->Mapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
  this->Mapper->SetInputConnection(this->ModelToWorldTransform->GetOutputPort());
  this->Mapper->SetColorModeToMapScalars();
  this->Mapper->ColorByArrayComponent("color", 0);
  this->Mapper->SetLookupTable(this->ColorTable);
  this->Mapper->ScalarVisibilityOn();
  this->Mapper->UseLookupTableScalarRangeOn();
  this->Mapper->SetTransformCoordinate(coordinate);

  this->Property = vtkSmartPointer<vtkProperty2D>::New();
  this->Property->SetPointSize(10.0);
  this->Property->SetLineWidth(2.0);

  this->Actor = vtkSmartPointer<vtkActor2D>::New();
  this->Actor->SetProperty(this->Property);
  this->Actor->SetMapper(this->Mapper);
}

//----------------------------------------------------------------------
vtkSlicerMarkupsWidgetRepresentation::MarkupsInteractionPipeline::~MarkupsInteractionPipeline()
= default;

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::MarkupsInteractionPipeline::InitializePipeline()
{
  this->CreateRotationHandles();
  this->CreateTranslationHandles();
  this->UpdateHandleColors();
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::MarkupsInteractionPipeline::CreateRotationHandles()
{
  vtkNew<vtkPoints> points;

  double xRotationHandle[3] = { 0, 1, 1 }; // X-axis
  vtkMath::Normalize(xRotationHandle);
  points->InsertNextPoint(xRotationHandle);
  double yRotationHandle[3] = { 1, 0, 1 }; // Y-axis
  vtkMath::Normalize(yRotationHandle);
  points->InsertNextPoint(yRotationHandle);
  double zRotationHandle[3] = { 1, 1, 0 }; // Z-axis
  vtkMath::Normalize(zRotationHandle);
  points->InsertNextPoint(zRotationHandle);
  this->RotationHandlePoints->SetPoints(points);

  vtkNew<vtkDoubleArray> orientationArray;
  orientationArray->SetName("orientation");
  orientationArray->SetNumberOfComponents(9);
  vtkNew<vtkTransform> xRotationOrientation;
  xRotationOrientation->RotateX(90);
  xRotationOrientation->RotateY(90);
  xRotationOrientation->RotateZ(45);
  vtkMatrix4x4* xRotationMatrix = xRotationOrientation->GetMatrix();
  orientationArray->InsertNextTuple9(xRotationMatrix->GetElement(0, 0), xRotationMatrix->GetElement(1, 0), xRotationMatrix->GetElement(2, 0),
                                     xRotationMatrix->GetElement(0, 1), xRotationMatrix->GetElement(1, 1), xRotationMatrix->GetElement(2, 1),
                                     xRotationMatrix->GetElement(0, 2), xRotationMatrix->GetElement(1, 2), xRotationMatrix->GetElement(2, 2));
  vtkNew<vtkTransform> yRotationOrientation;
  yRotationOrientation->RotateX(90);
  yRotationOrientation->RotateZ(45);
  vtkMatrix4x4* yRotationMatrix = yRotationOrientation->GetMatrix();
  orientationArray->InsertNextTuple9(yRotationMatrix->GetElement(0, 0), yRotationMatrix->GetElement(1, 0), yRotationMatrix->GetElement(2, 0),
                                     yRotationMatrix->GetElement(0, 1), yRotationMatrix->GetElement(1, 1), yRotationMatrix->GetElement(2, 1),
                                     yRotationMatrix->GetElement(0, 2), yRotationMatrix->GetElement(1, 2), yRotationMatrix->GetElement(2, 2));
  vtkNew<vtkTransform> zRotationOrientation;
  zRotationOrientation->RotateZ(45);
  vtkMatrix4x4* zRotationMatrix = zRotationOrientation->GetMatrix();
  orientationArray->InsertNextTuple9(zRotationMatrix->GetElement(0, 0), zRotationMatrix->GetElement(1, 0), zRotationMatrix->GetElement(2, 0),
                                     zRotationMatrix->GetElement(0, 1), zRotationMatrix->GetElement(1, 1), zRotationMatrix->GetElement(2, 1),
                                     zRotationMatrix->GetElement(0, 2), zRotationMatrix->GetElement(1, 2), zRotationMatrix->GetElement(2, 2));
  this->RotationHandlePoints->GetPointData()->AddArray(orientationArray);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::MarkupsInteractionPipeline::CreateTranslationHandles()
{
  vtkNew<vtkPoints> points;
  points->InsertNextPoint(1, 0, 0); // X-axis
  points->InsertNextPoint(0, 1, 0); // Y-axis
  points->InsertNextPoint(0, 0, 1); // Z-axis
  this->TranslationHandlePoints->SetPoints(points);

  vtkNew<vtkDoubleArray> orientationArray;
  orientationArray->SetName("orientation");
  orientationArray->SetNumberOfComponents(9);
  vtkNew<vtkTransform> xTranslationOrientation;
  vtkMatrix4x4* xTranslationMatrix = xTranslationOrientation->GetMatrix();
  orientationArray->InsertNextTuple9(xTranslationMatrix->GetElement(0, 0), xTranslationMatrix->GetElement(1, 0), xTranslationMatrix->GetElement(2, 0),
                                     xTranslationMatrix->GetElement(0, 1), xTranslationMatrix->GetElement(1, 1), xTranslationMatrix->GetElement(2, 1),
                                     xTranslationMatrix->GetElement(0, 2), xTranslationMatrix->GetElement(1, 2), xTranslationMatrix->GetElement(2, 2));
  vtkNew<vtkTransform> yTranslationOrientation;
  yTranslationOrientation->RotateZ(90);
  vtkMatrix4x4* yTranslationMatrix = yTranslationOrientation->GetMatrix();
  orientationArray->InsertNextTuple9(yTranslationMatrix->GetElement(0, 0), yTranslationMatrix->GetElement(1, 0), yTranslationMatrix->GetElement(2, 0),
                                     yTranslationMatrix->GetElement(0, 1), yTranslationMatrix->GetElement(1, 1), yTranslationMatrix->GetElement(2, 1),
                                     yTranslationMatrix->GetElement(0, 2), yTranslationMatrix->GetElement(1, 2), yTranslationMatrix->GetElement(2, 2));
  vtkNew<vtkTransform> zTranslationOrientation;
  zTranslationOrientation->RotateY(-90);
  vtkMatrix4x4* zTranslationMatrix = zTranslationOrientation->GetMatrix();
  orientationArray->InsertNextTuple9(zTranslationMatrix->GetElement(0, 0), zTranslationMatrix->GetElement(1, 0), zTranslationMatrix->GetElement(2, 0),
                                     zTranslationMatrix->GetElement(0, 1), zTranslationMatrix->GetElement(1, 1), zTranslationMatrix->GetElement(2, 1),
                                     zTranslationMatrix->GetElement(0, 2), zTranslationMatrix->GetElement(1, 2), zTranslationMatrix->GetElement(2, 2));
  this->TranslationHandlePoints->GetPointData()->AddArray(orientationArray);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::MarkupsInteractionPipeline::UpdateHandleColors()
{
  if (!this->ColorTable)
    {
    return;
    }

  int numberOfHandles = this->RotationHandlePoints->GetNumberOfPoints() + this->TranslationHandlePoints->GetNumberOfPoints();
  this->ColorTable->SetNumberOfTableValues(numberOfHandles);
  this->ColorTable->SetTableRange(0, numberOfHandles - 1);

  int colorIndex = 0;
  double color[4] = { 0 };

  // Rotation handles
  vtkSmartPointer<vtkDoubleArray> rotationColorArray = vtkDoubleArray::SafeDownCast(this->RotationHandlePoints->GetPointData()->GetArray("color"));
  if (!rotationColorArray)
    {
    rotationColorArray = vtkSmartPointer<vtkDoubleArray>::New();
    rotationColorArray->SetName("color");
    rotationColorArray->SetNumberOfComponents(1);
    this->RotationHandlePoints->GetPointData()->SetScalars(rotationColorArray);
    }
  rotationColorArray->Initialize();
  rotationColorArray->SetNumberOfTuples(this->RotationHandlePoints->GetNumberOfPoints());
  for (int i = 0; i < this->RotationHandlePoints->GetNumberOfPoints(); ++i)
    {
    this->GetHandleColor(vtkMRMLMarkupsDisplayNode::ComponentRotationHandle, i, color);
    this->ColorTable->SetTableValue(colorIndex, color);
    rotationColorArray->SetTuple1(i, colorIndex);
    ++colorIndex;
    }

  // Translation handles
  vtkSmartPointer<vtkDoubleArray> translationColorArray = vtkDoubleArray::SafeDownCast(this->TranslationHandlePoints->GetPointData()->GetArray("color"));
  if (!translationColorArray)
    {
    translationColorArray = vtkSmartPointer<vtkDoubleArray>::New();
    translationColorArray->SetName("color");
    translationColorArray->SetNumberOfComponents(1);
    this->TranslationHandlePoints->GetPointData()->SetScalars(translationColorArray);
    }
  translationColorArray->Initialize();
  translationColorArray->SetNumberOfTuples(this->TranslationHandlePoints->GetNumberOfPoints());
  for (int i = 0; i < this->TranslationHandlePoints->GetNumberOfPoints(); ++i)
    {
    this->GetHandleColor(vtkMRMLMarkupsDisplayNode::ComponentTranslationHandle, i, color);
    this->ColorTable->SetTableValue(colorIndex, color);
    translationColorArray->SetTuple1(i, colorIndex);
    ++colorIndex;
    }

  this->ColorTable->Build();
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::MarkupsInteractionPipeline::GetHandleColor(int type, int index, double color[4])
{
  if (!color)
    {
    return;
    }

  double red[4]    = { 1, 0, 0, 1 };
  double green[4]  = { 0, 1, 0, 1 };
  double blue[4]   = { 0, 0, 1, 1 };
  double yellow[4] = { 1, 1, 0, 1 };

  double* currentColor = red;
  switch (index)
    {
    case 0:
      currentColor = red;
      break;
    case 1:
      currentColor = blue;
      break;
    case 2:
      currentColor = green;
      break;
    default:
      return;
    }

  bool highlighted = false;
  // Highlighted
  vtkSlicerMarkupsWidgetRepresentation* markupsRepresentation = vtkSlicerMarkupsWidgetRepresentation::SafeDownCast(this->Representation);
  vtkMRMLMarkupsDisplayNode* displayNode = nullptr;
  if (markupsRepresentation)
    {
    displayNode = markupsRepresentation->GetMarkupsDisplayNode();
    }
  if (displayNode && displayNode->GetActiveComponentType() == type && displayNode->GetActiveComponentIndex() == index)
    {
    highlighted = true;
    currentColor = yellow;
    }

  for (int i = 0; i < 3; ++i)
    {
    color[i] = currentColor[i];
    }

  double opacity = 1.0;
  if (!highlighted)
    {
    opacity = this->GetOpacity(type, index);
    }
  color[3] = opacity;
}

//----------------------------------------------------------------------
double vtkSlicerMarkupsWidgetRepresentation::MarkupsInteractionPipeline::GetOpacity(int type, int index)
{
  double viewNormal[3] = { 0 };
  this->GetViewPlaneNormal(viewNormal);

  double opacity = 1.0;

  double axis[3] = { 0 };
  this->GetInteractionAxis(index, axis); // TODO
  if (vtkMath::Dot(viewNormal, axis) < 0)
    {
    vtkMath::MultiplyScalar(axis, -1);
    }
  double angle = vtkMath::DegreesFromRadians(vtkMath::AngleBetweenVectors(viewNormal, axis));

  double endFade = 10;
  double startFade = 20;
  if (type == vtkMRMLMarkupsDisplayNode::ComponentRotationHandle)
    {
    // TODO: Magic numbers
    if (angle > 90 - endFade)
      {
      opacity = 0.0;
      }
    else if (angle > 90 - startFade)
      {
      double difference = angle - (90 - startFade);
      opacity = 1.0 - (difference / endFade);
      }
    }
  else if (type == vtkMRMLMarkupsDisplayNode::ComponentTranslationHandle)
    {
    // TODO: Magic numbers
    if (angle < endFade)
      {
      opacity = 0.0;
      }
    else if (angle < startFade)
      {
      double difference = angle - endFade;
      opacity = (difference / endFade);
      }
    }
  return opacity;
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::MarkupsInteractionPipeline::GetViewPlaneNormal(double normal[3])
{
  if (!normal)
    {
    return;
    }
  if (this->Representation && this->Representation->GetRenderer() && this->Representation->GetRenderer()->GetActiveCamera())
    {
    vtkCamera* camera = this->Representation->GetRenderer()->GetActiveCamera();
    camera->GetViewPlaneNormal(normal);
    }
}

//----------------------------------------------------------------------
vtkSlicerMarkupsWidgetRepresentation::HandleInfoList vtkSlicerMarkupsWidgetRepresentation::MarkupsInteractionPipeline::GetHandleInfoList()
{
  vtkSlicerMarkupsWidgetRepresentation::HandleInfoList handleInfoList;
  for (int i = 0; i < this->RotationHandlePoints->GetNumberOfPoints(); ++i)
    {
    double handlePositionLocal[3] = { 0 };
    double handlePositionWorld[3] = { 0 };
    this->RotationHandlePoints->GetPoint(i, handlePositionLocal);
    this->RotationScaleTransform->GetTransform()->TransformPoint(handlePositionLocal, handlePositionWorld);
    this->ModelToWorldTransform->GetTransform()->TransformPoint(handlePositionWorld, handlePositionWorld);
    double color[4] = { 0 };
    this->GetHandleColor(vtkMRMLMarkupsDisplayNode::ComponentRotationHandle, i, color);
    HandleInfo info(i, vtkMRMLMarkupsDisplayNode::ComponentRotationHandle, handlePositionWorld, handlePositionLocal, color);
    handleInfoList.push_back(info);
    }

  for (int i = 0; i < this->TranslationHandlePoints->GetNumberOfPoints(); ++i)
    {
    double handlePositionLocal[3] = { 0 };
    double handlePositionWorld[3] = { 0 };
    this->TranslationHandlePoints->GetPoint(i, handlePositionLocal);
    this->TranslationScaleTransform->GetTransform()->TransformPoint(handlePositionLocal, handlePositionWorld);
    this->ModelToWorldTransform->GetTransform()->TransformPoint(handlePositionWorld, handlePositionWorld);
    double color[4] = { 0 };
    this->GetHandleColor(vtkMRMLMarkupsDisplayNode::ComponentTranslationHandle, i, color);
    HandleInfo info(i, vtkMRMLMarkupsDisplayNode::ComponentTranslationHandle, handlePositionWorld, handlePositionLocal, color);
    handleInfoList.push_back(info);
    }
  return handleInfoList;
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::MarkupsInteractionPipeline::SetWidgetScale(double scale)
{
  vtkNew<vtkTransform> scaleTransform;
  scaleTransform->Scale(scale, scale, scale);
  this->RotationScaleTransform->SetTransform(scaleTransform);
  this->TranslationScaleTransform->SetTransform(scaleTransform);
  this->AxisRotationGlypher->SetScaleFactor(scale);
  this->AxisTranslationGlypher->SetScaleFactor(scale);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::MarkupsInteractionPipeline::GetInteractionAxis(int index, double axis[3])
{
  if (!axis || index < 0 || index > 2)
    {
    // TODO add to pipeline?
    return;
    }

  double modelAxis[3] = { 0 };
  modelAxis[index] = 1;
  double origin[3] = { 0,0,0 };
  this->ModelToWorldTransform->GetTransform()->TransformVectorAtPoint(origin, modelAxis, axis);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::MarkupsInteractionPipeline::GetInteractionOrigin(double origin[3])
{
  if (!origin)
    {
    // TODO add to pipeline?
    return;
    }

  double tempOrigin[3] = { 0,0,0 };
  this->ModelToWorldTransform->GetTransform()->TransformPoint(tempOrigin, origin);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::MarkupsInteractionPipeline::GetInteractionModelToWorldMatrix(vtkTransform* matrix)
{
  if (!matrix)
    {
    return;
    }

  if (this->ModelToWorldTransform->GetTransform())
    {
    matrix->DeepCopy(this->ModelToWorldTransform->GetTransform());
    }
}
//----------------------------------------------------------------------
void vtkSlicerMarkupsWidgetRepresentation::GetInteractionHandlePositionWorld(int type, int index, double position[3])
{
  if (type == vtkMRMLMarkupsDisplayNode::ComponentRotationHandle)
    {
    this->InteractionPipeline->RotationHandlePoints->GetPoint(index, position);
    this->InteractionPipeline->RotationScaleTransform->GetTransform()->TransformPoint(position, position);
    this->InteractionPipeline->ModelToWorldTransform->GetTransform()->TransformPoint(position, position);
    }
  else if (type == vtkMRMLMarkupsDisplayNode::ComponentTranslationHandle)
    {
    this->InteractionPipeline->TranslationHandlePoints->GetPoint(index, position);
    this->InteractionPipeline->TranslationScaleTransform->GetTransform()->TransformPoint(position, position);
    this->InteractionPipeline->ModelToWorldTransform->GetTransform()->TransformPoint(position, position);
    }
}