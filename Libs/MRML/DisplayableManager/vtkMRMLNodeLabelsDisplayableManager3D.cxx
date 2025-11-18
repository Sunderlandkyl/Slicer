/*==============================================================================

  Program: 3D Slicer

  Copyright (c) Brigham and Women's Hospital

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

// MRMLDisplayableManager includes
#include "vtkMRMLNodeLabelsDisplayableManager3D.h"
#include "vtkMRMLLabelDisplayNode.h"

// MRML includes
#include <vtkMRMLDisplayableNode.h>
#include <vtkMRMLScene.h>
#include <vtkMRMLSegmentationNode.h>
#include <vtkMRMLViewNode.h>

// VTK includes
#include <vtkActor2D.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCoordinate.h>
#include <vtkDoubleArray.h>
#include <vtkFloatArray.h>
#include <vtkLabelPlacementMapper.h>
#include <vtkLine.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPointSetToLabelHierarchy.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkProperty2D.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>
#include <vtkUnsignedCharArray.h>

// STD includes
#include <algorithm>
#include <map>
#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Renderer update observer callback
class vtkRendererUpdateObserver : public vtkCommand
{
public:
  static vtkRendererUpdateObserver* New() { return new vtkRendererUpdateObserver; }
  void Execute(vtkObject* vtkNotUsed(wdg), unsigned long vtkNotUsed(event), void* vtkNotUsed(calldata)) override
  {
    if (this->DisplayableManager)
    {
      this->DisplayableManager->UpdateFromRenderer();
    }
  }
  vtkWeakPointer<vtkMRMLNodeLabelsDisplayableManager3D> DisplayableManager;
};

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLNodeLabelsDisplayableManager3D);

//----------------------------------------------------------------------------
// vtkInternal helper class

//---------------------------------------------------------------------------
class vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal
{
public:
  vtkInternal(vtkMRMLNodeLabelsDisplayableManager3D* external);
  ~vtkInternal();

  struct DisplayNodeInfo
  {
    vtkMRMLLabelDisplayNode* DisplayNode{nullptr};

    // Label rendering using vtkLabelPlacementMapper (efficient!)
    vtkSmartPointer<vtkPolyData> LabelPolyData;
    vtkSmartPointer<vtkPoints> LabelPoints;
    vtkSmartPointer<vtkStringArray> Labels;
    vtkSmartPointer<vtkFloatArray> LabelPriority;
    vtkSmartPointer<vtkPointSetToLabelHierarchy> LabelHierarchy;
    vtkSmartPointer<vtkLabelPlacementMapper> LabelMapper;
    vtkSmartPointer<vtkActor2D> LabelActor;

    // Lines from anchor to label
    vtkSmartPointer<vtkPolyData> LinePolyData;
    vtkSmartPointer<vtkPolyDataMapper2D> LineMapper;
    vtkSmartPointer<vtkActor2D> LineActor;

    // Cache for collision avoidance
    struct LabelCache
    {
      double AnchorWorld[3]{0,0,0};
      double AnchorDisplay[2]{0,0};
      double AssignedDisplay[2]{0,0};
      int LabelPosition{0};
      bool Visible{false};
    };
    std::vector<LabelCache> CachedLabels;
  };

  std::map<vtkMRMLLabelDisplayNode*, DisplayNodeInfo> DisplayNodes;

  vtkMRMLNodeLabelsDisplayableManager3D* External;

  void AddDisplayNode(vtkMRMLLabelDisplayNode* displayNode);
  void UpdateDisplayNode(vtkMRMLLabelDisplayNode* displayNode);
  void RemoveDisplayNode(vtkMRMLLabelDisplayNode* displayNode);
  void RemoveAllDisplayNodes();

  void WorldToDisplay(const double worldPos[3], double displayPos[2]);
  void UpdateLabels();

  void AddRendererUpdateObserver(vtkRenderer* renderer);
  void RemoveRendererUpdateObserver();

  // Helper to safely get renderer (uses cached pointer instead of calling through External)
  vtkRenderer* GetRenderer() { return this->ObservedRenderer; }

  vtkSmartPointer<vtkRendererUpdateObserver> RendererUpdateObserver;
  vtkWeakPointer<vtkRenderer> ObservedRenderer;
  unsigned long RendererUpdateObservationId{ 0 };
};

//---------------------------------------------------------------------------
vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::vtkInternal(vtkMRMLNodeLabelsDisplayableManager3D* external)
{
  this->External = external;
  this->RendererUpdateObserver = vtkSmartPointer<vtkRendererUpdateObserver>::New();
  this->RendererUpdateObserver->DisplayableManager = external;
}

//---------------------------------------------------------------------------
vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::~vtkInternal()
{
  // Remove observer first to prevent callbacks during destruction
  this->RemoveRendererUpdateObserver();
  this->RemoveAllDisplayNodes();
  this->RendererUpdateObserver = nullptr;
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::AddDisplayNode(vtkMRMLLabelDisplayNode* displayNode)
{
  if (!displayNode || !displayNode->GetID())
  {
    return;
  }

  if (this->DisplayNodes.find(displayNode) != this->DisplayNodes.end())
  {
    return; // Already exists
  }

  DisplayNodeInfo info;
  info.DisplayNode = displayNode;

  // Create polydata for labels
  info.LabelPolyData = vtkSmartPointer<vtkPolyData>::New();
  info.LabelPoints = vtkSmartPointer<vtkPoints>::New();
  info.LabelPolyData->SetPoints(info.LabelPoints);

  info.Labels = vtkSmartPointer<vtkStringArray>::New();
  info.Labels->SetName("labels");
  info.LabelPolyData->GetPointData()->AddArray(info.Labels);

  info.LabelPriority = vtkSmartPointer<vtkFloatArray>::New();
  info.LabelPriority->SetName("priority");
  info.LabelPolyData->GetPointData()->AddArray(info.LabelPriority);

  // Set up label hierarchy and mapper
  info.LabelHierarchy = vtkSmartPointer<vtkPointSetToLabelHierarchy>::New();
  info.LabelHierarchy->SetInputData(info.LabelPolyData);
  info.LabelHierarchy->SetLabelArrayName("labels");
  info.LabelHierarchy->SetPriorityArrayName("priority");

  info.LabelMapper = vtkSmartPointer<vtkLabelPlacementMapper>::New();
  info.LabelMapper->SetInputConnection(info.LabelHierarchy->GetOutputPort());
  info.LabelMapper->PlaceAllLabelsOn(); // We want all labels shown

  // Set default text property
  vtkNew<vtkTextProperty> textProperty;
  textProperty->SetFontSize(10); // Base font size
  textProperty->SetColor(displayNode->GetColor()); // Use display node color
  textProperty->SetBold(0);
  textProperty->SetItalic(0);
  textProperty->SetShadow(1);
  textProperty->SetFontFamilyToArial();
  info.LabelHierarchy->SetTextProperty(textProperty);

  info.LabelActor = vtkSmartPointer<vtkActor2D>::New();
  info.LabelActor->SetMapper(info.LabelMapper);
  info.LabelActor->PickableOff();
  info.LabelActor->DragableOff();

  // Create polydata for lines
  info.LinePolyData = vtkSmartPointer<vtkPolyData>::New();
  vtkNew<vtkPoints> linePoints;
  info.LinePolyData->SetPoints(linePoints);
  vtkNew<vtkCellArray> lines;
  info.LinePolyData->SetLines(lines);

  info.LineMapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
  info.LineMapper->SetInputData(info.LinePolyData);

  info.LineActor = vtkSmartPointer<vtkActor2D>::New();
  info.LineActor->SetMapper(info.LineMapper);
  info.LineActor->GetProperty()->SetLineWidth(2.0);
  info.LineActor->PickableOff();
  info.LineActor->DragableOff();

  if (this->GetRenderer())
  {
    this->GetRenderer()->AddActor2D(info.LabelActor);
    this->GetRenderer()->AddActor2D(info.LineActor);
  }

  this->DisplayNodes[displayNode] = info;
  this->UpdateDisplayNode(displayNode);
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::UpdateDisplayNode(vtkMRMLLabelDisplayNode* displayNode)
{
  if (!displayNode)
  {
    return;
  }

  auto it = this->DisplayNodes.find(displayNode);
  if (it == this->DisplayNodes.end())
  {
    return;
  }

  // Just mark for update; actual update happens in UpdateLabels()
  this->External->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::RemoveDisplayNode(vtkMRMLLabelDisplayNode* displayNode)
{
  auto it = this->DisplayNodes.find(displayNode);
  if (it == this->DisplayNodes.end())
  {
    return;
  }

  if (this->GetRenderer())
  {
    this->GetRenderer()->RemoveActor2D(it->second.LabelActor);
    this->GetRenderer()->RemoveActor2D(it->second.LineActor);
  }

  this->DisplayNodes.erase(it);
  this->External->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::RemoveAllDisplayNodes()
{
  for (auto& pair : this->DisplayNodes)
  {
    if (this->GetRenderer())
    {
      this->GetRenderer()->RemoveActor2D(pair.second.LabelActor);
      this->GetRenderer()->RemoveActor2D(pair.second.LineActor);
    }
  }
  this->DisplayNodes.clear();
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::WorldToDisplay(const double worldPos[3], double displayPos[2])
{
  if (!this->GetRenderer())
  {
    displayPos[0] = 0;
    displayPos[1] = 0;
    return;
  }

  vtkNew<vtkCoordinate> coordinate;
  coordinate->SetCoordinateSystemToWorld();
  coordinate->SetValue(worldPos[0], worldPos[1], worldPos[2]);
  int* display = coordinate->GetComputedDisplayValue(this->GetRenderer());
  displayPos[0] = display[0];
  displayPos[1] = display[1];
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::UpdateLabels()
{
  if (!this->GetRenderer())
  {
    return;
  }

  int* viewportSize = this->GetRenderer()->GetSize();
  int margin = 10;

  // Update all display nodes
  for (auto& pair : this->DisplayNodes)
  {
    vtkMRMLLabelDisplayNode* displayNode = pair.first;
    DisplayNodeInfo& info = pair.second;

    if (!displayNode)
    {
      continue;
    }

    int labelCount = displayNode->GetNumberOfLabels();
    info.CachedLabels.resize(labelCount);

    // Clear arrays
    info.LabelPoints->Reset();
    info.Labels->Reset();
    info.LabelPriority->Reset();

    // Build label data
    for (int i = 0; i < labelCount; ++i)
    {
      vtkMRMLLabelDisplayNode::LabelInfo labelInfo;
      if (!displayNode->GetLabelInfo(i, labelInfo) || !labelInfo.Visible)
      {
        info.CachedLabels[i].Visible = false;
        continue;
      }

      // Store cache
      auto& cache = info.CachedLabels[i];
      cache.Visible = true;
      cache.AnchorWorld[0] = labelInfo.AnchorPosition[0];
      cache.AnchorWorld[1] = labelInfo.AnchorPosition[1];
      cache.AnchorWorld[2] = labelInfo.AnchorPosition[2];
      cache.LabelPosition = labelInfo.LabelPosition;

      // Convert to display
      this->WorldToDisplay(cache.AnchorWorld, cache.AnchorDisplay);

      // Initial assigned position (will be adjusted for collision)
      cache.AssignedDisplay[0] = cache.AnchorDisplay[0];
      cache.AssignedDisplay[1] = cache.AnchorDisplay[1];
    }

    // Apply edge positioning and collision avoidance
    const int minGap = 4;
    auto adjustGroup = [&](int side, bool verticalStack)
    {
      std::vector<int> indices;
      for (int i = 0; i < labelCount; ++i)
      {
        if (info.CachedLabels[i].Visible && info.CachedLabels[i].LabelPosition == side)
        {
          indices.push_back(i);
        }
      }

      if (indices.size() < 2)
      {
        return;
      }

      if (verticalStack)
      {
        std::sort(indices.begin(), indices.end(), [&](int a, int b) {
          return info.CachedLabels[a].AnchorDisplay[1] < info.CachedLabels[b].AnchorDisplay[1];
        });

        for (size_t j = 1; j < indices.size(); ++j)
        {
          int prevIdx = indices[j - 1];
          int currIdx = indices[j];
          int neededY = static_cast<int>(info.CachedLabels[prevIdx].AssignedDisplay[1]) + minGap + 15; // Assume ~15px label height
          if (info.CachedLabels[currIdx].AssignedDisplay[1] < neededY)
          {
            info.CachedLabels[currIdx].AssignedDisplay[1] = neededY;
          }
        }
      }
      else
      {
        std::sort(indices.begin(), indices.end(), [&](int a, int b) {
          return info.CachedLabels[a].AnchorDisplay[0] < info.CachedLabels[b].AnchorDisplay[0];
        });

        for (size_t j = 1; j < indices.size(); ++j)
        {
          int prevIdx = indices[j - 1];
          int currIdx = indices[j];
          int neededX = static_cast<int>(info.CachedLabels[prevIdx].AssignedDisplay[0]) + minGap + 50; // Assume ~50px label width
          if (info.CachedLabels[currIdx].AssignedDisplay[0] < neededX)
          {
            info.CachedLabels[currIdx].AssignedDisplay[0] = neededX;
          }
        }
      }
    };

    // Apply edge snapping
    for (int i = 0; i < labelCount; ++i)
    {
      if (!info.CachedLabels[i].Visible)
      {
        continue;
      }

      auto& cache = info.CachedLabels[i];
      switch (cache.LabelPosition)
      {
        case vtkMRMLLabelDisplayNode::LabelPositionLeft:
          cache.AssignedDisplay[0] = margin;
          cache.AssignedDisplay[1] = cache.AnchorDisplay[1];
          break;
        case vtkMRMLLabelDisplayNode::LabelPositionRight:
          cache.AssignedDisplay[0] = viewportSize[0] - margin - 50; // Approx label width
          cache.AssignedDisplay[1] = cache.AnchorDisplay[1];
          break;
        case vtkMRMLLabelDisplayNode::LabelPositionTop:
          cache.AssignedDisplay[0] = cache.AnchorDisplay[0];
          cache.AssignedDisplay[1] = viewportSize[1] - margin - 15; // Approx label height
          break;
        case vtkMRMLLabelDisplayNode::LabelPositionBottom:
          cache.AssignedDisplay[0] = cache.AnchorDisplay[0];
          cache.AssignedDisplay[1] = margin;
          break;
      }
    }

    // Apply collision avoidance
    adjustGroup(vtkMRMLLabelDisplayNode::LabelPositionLeft, true);
    adjustGroup(vtkMRMLLabelDisplayNode::LabelPositionRight, true);
    adjustGroup(vtkMRMLLabelDisplayNode::LabelPositionTop, false);
    adjustGroup(vtkMRMLLabelDisplayNode::LabelPositionBottom, false);

    // Now populate the label polydata and line polydata
    vtkNew<vtkPoints> linePoints;
    vtkNew<vtkCellArray> lines;
    vtkNew<vtkUnsignedCharArray> lineColors;
    lineColors->SetNumberOfComponents(3);
    lineColors->SetName("Colors");

    for (int i = 0; i < labelCount; ++i)
    {
      if (!info.CachedLabels[i].Visible)
      {
        continue;
      }

      vtkMRMLLabelDisplayNode::LabelInfo labelInfo;
      displayNode->GetLabelInfo(i, labelInfo);
      auto& cache = info.CachedLabels[i];

      // Convert assigned display position back to world coordinates for 3D
      // Use the anchor's world position but shift it to the display offset
      double labelWorldPos[3];
      labelWorldPos[0] = cache.AnchorWorld[0];
      labelWorldPos[1] = cache.AnchorWorld[1];
      labelWorldPos[2] = cache.AnchorWorld[2];

      // For 3D, we need to place the label in world space at a position that appears
      // at the assigned display position. We'll use the anchor world Z and offset in screen space.
      // Simple approach: just use anchor world position (the mapper will handle placement)
      info.LabelPoints->InsertNextPoint(labelWorldPos[0], labelWorldPos[1], labelWorldPos[2]);
      info.Labels->InsertNextValue(labelInfo.Text);
      info.LabelPriority->InsertNextValue(static_cast<float>(i));

      // Add line from anchor to label (in display coordinates for 2D polydata mapper)
      if (labelInfo.LineVisible)
      {
        vtkIdType ptId0 = linePoints->InsertNextPoint(cache.AnchorDisplay[0], cache.AnchorDisplay[1], 0.0);
        vtkIdType ptId1 = linePoints->InsertNextPoint(cache.AssignedDisplay[0], cache.AssignedDisplay[1], 0.0);
        vtkNew<vtkLine> line;
        line->GetPointIds()->SetId(0, ptId0);
        line->GetPointIds()->SetId(1, ptId1);
        lines->InsertNextCell(line);

        // Add line color
        unsigned char color[3];
        color[0] = static_cast<unsigned char>(labelInfo.Color[0] * 255);
        color[1] = static_cast<unsigned char>(labelInfo.Color[1] * 255);
        color[2] = static_cast<unsigned char>(labelInfo.Color[2] * 255);
        lineColors->InsertNextTypedTuple(color);
      }
    }

    // Update polydata
    info.LabelPoints->Modified();
    info.LabelPolyData->Modified();

    info.LinePolyData->SetPoints(linePoints);
    info.LinePolyData->SetLines(lines);
    info.LinePolyData->GetCellData()->SetScalars(lineColors);
    info.LinePolyData->Modified();

    // Enable scalar coloring for lines
    info.LineMapper->SetScalarModeToUseCellData();
    info.LineMapper->ScalarVisibilityOn();

    // Set visibility
    info.LabelActor->SetVisibility(labelCount > 0);
    info.LineActor->SetVisibility(labelCount > 0);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::AddRendererUpdateObserver(vtkRenderer* renderer)
{
  RemoveRendererUpdateObserver();
  if (renderer)
  {
    this->ObservedRenderer = renderer;
    this->RendererUpdateObservationId = renderer->AddObserver(vtkCommand::StartEvent, this->RendererUpdateObserver);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::RemoveRendererUpdateObserver()
{
  if (this->ObservedRenderer)
  {
    this->ObservedRenderer->RemoveObserver(this->RendererUpdateObservationId);
    this->ObservedRenderer = nullptr;
    this->RendererUpdateObservationId = 0;
  }
}

//----------------------------------------------------------------------------
// vtkMRMLNodeLabelsDisplayableManager3D methods

//----------------------------------------------------------------------------
vtkMRMLNodeLabelsDisplayableManager3D::vtkMRMLNodeLabelsDisplayableManager3D()
{
  this->Internal = new vtkInternal(this);
}

//----------------------------------------------------------------------------
vtkMRMLNodeLabelsDisplayableManager3D::~vtkMRMLNodeLabelsDisplayableManager3D()
{
  delete this->Internal;
  this->Internal = nullptr;
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "NodeLabelsDisplayableManager3D" << std::endl;
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::Create()
{
  this->Superclass::Create();
  this->Internal->AddRendererUpdateObserver(this->GetRenderer());
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::AdditionalInitializeStep()
{
  // Observe all node label display nodes in the scene
  /*this->AddMRMLSceneObservation();*/
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::UpdateFromRenderer()
{
  // Safety check: don't update if being destroyed
  if (!this->Internal || !this->GetRenderer())
  {
    return;
  }
  // Update label positions when camera moves
  this->Internal->UpdateLabels();
  this->RequestRender();
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::UnobserveMRMLScene()
{
  this->Internal->RemoveAllDisplayNodes();
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::OnMRMLSceneNodeAdded(vtkMRMLNode* node)
{
  if (!node || !this->GetMRMLScene())
  {
    return;
  }

  if (node->IsA("vtkMRMLLabelDisplayNode"))
  {
    vtkMRMLLabelDisplayNode* displayNode = vtkMRMLLabelDisplayNode::SafeDownCast(node);
    this->Internal->AddDisplayNode(displayNode);

    // Observe the display node
    vtkNew<vtkIntArray> events;
    events->InsertNextValue(vtkCommand::ModifiedEvent);
    events->InsertNextValue(vtkMRMLLabelDisplayNode::LabelTextModifiedEvent);
    events->InsertNextValue(vtkMRMLLabelDisplayNode::AnchorPositionModifiedEvent);
    events->InsertNextValue(vtkMRMLLabelDisplayNode::LabelPropertiesModifiedEvent);
    vtkObserveMRMLNodeEventsMacro(displayNode, events);
  }
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::OnMRMLSceneNodeRemoved(vtkMRMLNode* node)
{
  if (!node)
  {
    return;
  }

  if (node->IsA("vtkMRMLLabelDisplayNode"))
  {
    vtkMRMLLabelDisplayNode* displayNode = vtkMRMLLabelDisplayNode::SafeDownCast(node);
    this->Internal->RemoveDisplayNode(displayNode);
    vtkUnObserveMRMLNodeMacro(displayNode);
  }
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::ProcessMRMLNodesEvents(vtkObject* caller, unsigned long event, void* callData)
{
  vtkMRMLLabelDisplayNode* displayNode = vtkMRMLLabelDisplayNode::SafeDownCast(caller);
  if (displayNode)
  {
    this->Internal->UpdateDisplayNode(displayNode);
    this->Internal->UpdateLabels();
  }

  this->Superclass::ProcessMRMLNodesEvents(caller, event, callData);
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::OnMRMLDisplayableNodeModifiedEvent(vtkObject* caller)
{
  // Update all labels when view is modified
  this->Internal->UpdateLabels();
  this->RequestRender();
}
